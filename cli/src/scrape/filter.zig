//! Which of a page's media survive: the kind/format tokens, the dimension bounds and the
//! optional --source-name regex. The pattern is length-capped before it reaches regcomp.
const std = @import("std");
const builtin = @import("builtin");
const image = @import("../image.zig");
const mediaTypes = @import("../mediaTypes.zig");
const testing = std.testing;
const text = @import("text.zig");
const sniff = @import("sniff.zig");
const Sniff = sniff.Sniff;
const indexOfPosCI = text.indexOfPosCI;

// The --source-name filter is a regex on the media URL: POSIX targets use the platform libc's regex.h
// through src/regex_shim.c (no new dependency); Windows/WASI fall back to a substring test.
pub const has_posix_regex = builtin.os.tag != .windows and builtin.os.tag != .wasi;
extern fn stencil_regex_compile(pattern: [*:0]const u8) ?*anyopaque;
extern fn stencil_regex_match(handle: ?*anyopaque, text: [*:0]const u8) c_int;
extern fn stencil_regex_free(handle: ?*anyopaque) void;

/// Longest pattern accepted: glibc's regexec can backtrack catastrophically on a crafted one,
/// and this is model-controlled the moment an adapter forwards it.
pub const max_name_pattern = 200;

/// A compiled `--source-name` matcher. POSIX: a case-insensitive extended regex; elsewhere: a
/// case-insensitive substring test. An absent/empty pattern matches everything.
pub const NameMatcher = struct {
    active: bool = false,
    pattern: []const u8 = "",
    handle: ?*anyopaque = null, // POSIX: opaque regex_t owned by the C shim; null = inactive

    /// Compile `pattern`; error.BadNamePattern on an invalid regex, error.NamePatternTooLong
    /// past the cap. `arena` owns the NUL-terminated copy handed to the shim's regcomp.
    pub fn init(pattern: ?[]const u8, arena: std.mem.Allocator) !NameMatcher {
        const p = pattern orelse return .{};
        if (p.len == 0) return .{};
        if (p.len > max_name_pattern) return error.NamePatternTooLong;
        if (has_posix_regex) {
            const pz = try arena.dupeZ(u8, p);
            const handle = stencil_regex_compile(pz.ptr) orelse return error.BadNamePattern;
            return .{ .active = true, .pattern = p, .handle = handle };
        }
        return .{ .active = true, .pattern = p };
    }

    pub fn deinit(self: *NameMatcher) void {
        if (has_posix_regex and self.handle != null) {
            stencil_regex_free(self.handle);
            self.handle = null;
        }
    }

    /// Does `url` match? An inactive matcher passes everything. The POSIX scratch NUL copy that
    /// regexec needs fails closed on OOM (drops the item).
    pub fn matches(self: *NameMatcher, url: []const u8, arena: std.mem.Allocator) bool {
        if (!self.active) return true;
        if (has_posix_regex) {
            const uz = arena.dupeZ(u8, url) catch return false;
            return stencil_regex_match(self.handle, uz.ptr) != 0;
        }
        return indexOfPosCI(url, 0, self.pattern) != null;
    }
};

pub const Kind = enum { img, bg, video, poster };

/// One extracted media item. `url` is the absolute http(s) URL (owned by the parse allocator);
/// `is_poster` promotes the item to the `poster` category regardless of kind.
pub const Media = struct {
    url: []const u8,
    kind: Kind,
    alt: []const u8 = "",
    is_poster: bool = false,
    width: u32 = 0, // measured later (0 = unknown)
    height: u32 = 0,

    /// The user-facing category token this item filters under (§1). A poster tag wins.
    pub fn category(self: Media) []const u8 {
        if (self.is_poster) return "poster";
        return switch (self.kind) {
            .img => "img",
            .bg => "background",
            .video => "video",
            .poster => "poster",
        };
    }
};

// format derivation (port of extension formatOf + norm)

/// Lowercase, normalized media "format" token for a URL / data: URI, written into `buf`; ""
/// when none. Port of the extension's `formatOf`+`norm`. `buf` needs ~16 bytes.
pub fn formatOf(buf: []u8, url: []const u8) []const u8 {
    if (url.len == 0) return "";
    if (std.ascii.startsWithIgnoreCase(url, "data:")) {
        // data:(image|video)/<subtype>[;...]
        const rest = url["data:".len..];
        const img_pfx = mediaTypes.imagePrefix();
        const vid_pfx = mediaTypes.videoPrefix();
        const sub = if (std.ascii.startsWithIgnoreCase(rest, img_pfx))
            rest[img_pfx.len..]
        else if (std.ascii.startsWithIgnoreCase(rest, vid_pfx))
            rest[vid_pfx.len..]
        else
            return "";
        var n: usize = 0;
        while (n < sub.len) : (n += 1) {
            const c = sub[n];
            const ok = std.ascii.isAlphanumeric(c) or c == '.' or c == '+' or c == '-';
            if (!ok) break;
        }
        return norm(buf, sub[0..n]);
    }
    const path = pathnameOf(url);
    const dot = std.mem.lastIndexOfScalar(u8, path, '.') orelse return "";
    const ext = path[dot + 1 ..];
    if (!mediaTypes.extLenOk(ext.len)) return "";
    for (ext) |c| if (!std.ascii.isAlphanumeric(c)) return "";
    return norm(buf, ext);
}

/// Lowercase `ext` into `buf`, then apply mediaTypes.json's SUBSTRING normalizations in its order —
/// matching the extension's chained `String.replace` and the pystencil port. Every replacement shrinks.
fn norm(buf: []u8, ext: []const u8) []const u8 {
    const n = @min(ext.len, buf.len);
    _ = std.ascii.lowerString(buf[0..n], ext[0..n]);
    var scratch: [64]u8 = undefined;
    var cur: []const u8 = buf[0..n];
    for (mediaTypes.normalizations()) |pair| {
        if (std.mem.indexOf(u8, cur, pair[0]) != null) {
            const sz = std.mem.replacementSize(u8, cur, pair[0], pair[1]);
            _ = std.mem.replace(u8, cur, pair[0], pair[1], scratch[0..sz]);
            @memcpy(buf[0..sz], scratch[0..sz]);
            cur = buf[0..sz];
        }
    }
    return cur;
}

/// The pathname of a URL: after `scheme://host`, up to the first `?` or `#`. A URL with no
/// scheme is treated as all-path.
pub fn pathnameOf(url: []const u8) []const u8 {
    var start: usize = 0;
    if (std.mem.indexOf(u8, url, "://")) |s| {
        const after = s + 3;
        start = if (std.mem.indexOfScalarPos(u8, url, after, '/')) |slash| slash else url.len;
    }
    var end = url.len;
    if (std.mem.indexOfAnyPos(u8, url, start, "?#")) |q| end = q;
    return url[start..end];
}

/// The format token an item filters on: images/backgrounds/posters key on the item URL,
/// videos on their media URL (already the item URL here). "" buckets as `etc`.
fn formatTokenOf(buf: []u8, m: Media) []const u8 {
    const f = formatOf(buf, m.url);
    return if (f.len == 0) "etc" else f;
}

// filters (port of filters.js)

/// True when `token` is selected by a `|`-separated list. Empty list or an `all` entry
/// means every token passes.
pub fn tokenSelected(list: []const u8, token: []const u8) bool {
    if (std.mem.trim(u8, list, " \t").len == 0) return true;
    var it = std.mem.splitScalar(u8, list, '|');
    while (it.next()) |raw| {
        const t = std.mem.trim(u8, raw, " \t");
        if (std.ascii.eqlIgnoreCase(t, "all")) return true;
        if (std.ascii.eqlIgnoreCase(t, token)) return true;
    }
    return false;
}

pub fn categoryPass(m: Media, filter: []const u8) bool {
    return tokenSelected(filter, m.category());
}

pub fn formatPass(m: Media, formats: []const u8) bool {
    var buf: [16]u8 = undefined;
    return tokenSelected(formats, formatTokenOf(&buf, m));
}

/// Inclusive width/height bound check (port of filters.js:81-88): each bound applies only when set, an
/// unmeasured item passes unconditionally, and a measured axis fails only when `< min` or `> max`.
pub fn dimensionPass(dims: ?Sniff, min_w: ?u32, max_w: ?u32, min_h: ?u32, max_h: ?u32) bool {
    const d = dims orelse return true;
    if (d.width > 0) {
        if (min_w) |v| if (d.width < v) return false;
        if (max_w) |v| if (d.width > v) return false;
    }
    if (d.height > 0) {
        if (min_h) |v| if (d.height < v) return false;
        if (max_h) |v| if (d.height > v) return false;
    }
    return true;
}

test "formatOf: path, query, data, normalization" {
    var b: [16]u8 = undefined;
    try testing.expectEqualStrings("png", formatOf(&b, "http://x/a/logo.png"));
    try testing.expectEqualStrings("jpg", formatOf(&b, "http://x/p.JPEG?v=2"));
    try testing.expectEqualStrings("jpg", formatOf(&b, "http://x/p.jpg#frag"));
    try testing.expectEqualStrings("webp", formatOf(&b, "https://cdn.test/a.b.webp"));
    try testing.expectEqualStrings("svg", formatOf(&b, "data:image/svg+xml;base64,AAAA"));
    try testing.expectEqualStrings("png", formatOf(&b, "data:image/png;base64,AAAA"));
    try testing.expectEqualStrings("mov", formatOf(&b, "http://x/clip.MOV"));
    try testing.expectEqualStrings("mov", formatOf(&b, "data:video/quicktime,xx"));
    // norm is a SUBSTRING replacement (matches the extension's chained .replace): a data:
    // subtype like x-jpeg has its jpeg→jpg substring rewritten.
    try testing.expectEqualStrings("x-jpg", formatOf(&b, "data:image/x-jpeg;base64,AA"));
    try testing.expectEqualStrings("", formatOf(&b, "http://example.com")); // domain dot is not an ext
    try testing.expectEqualStrings("", formatOf(&b, "http://x/noext"));
    try testing.expectEqualStrings("", formatOf(&b, ""));
}

test "tokenSelected: all / subset / etc" {
    try testing.expect(tokenSelected("all", "img"));
    try testing.expect(tokenSelected("", "video"));
    try testing.expect(tokenSelected("png|jpg", "jpg"));
    try testing.expect(!tokenSelected("png|jpg", "webp"));
    try testing.expect(tokenSelected("img|video", "video"));
    try testing.expect(!tokenSelected("img", "background"));
}

test "dimensionPass: inclusive bounds, unknown passes" {
    const d = Sniff{ .width = 200, .height = 100, .fmt = "png" };
    try testing.expect(dimensionPass(d, 100, null, null, null));
    try testing.expect(dimensionPass(d, 200, 200, 100, 100)); // inclusive
    try testing.expect(!dimensionPass(d, 201, null, null, null)); // below min width
    try testing.expect(!dimensionPass(d, null, 199, null, null)); // above max width
    try testing.expect(!dimensionPass(d, null, null, null, 99)); // above max height
    try testing.expect(dimensionPass(null, 500, 600, 500, 600)); // unknown size passes
}

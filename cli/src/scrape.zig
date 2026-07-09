//! Source-site scraping: fetch a web page, parse its HTML (no third-party libs), extract
//! image / video / background / poster media URLs, filter them (category / format /
//! dimension), and download the matches into an output DIRECTORY. Adapter-only — the C++
//! core is never touched (HTML parsing is adapter territory).
//!
//! Reference implementation of the cross-surface scrape contract (pystencil mirrors it).
//! The parsing/filter semantics are a port of the Chrome extension
//! (extension/src/lib/imageScan.js + filters.js) with the static-HTML adaptations noted in
//! the DESIGN contract: no computed style / currentSrc, so <img> falls back to lazy attrs
//! and backgrounds cover inline `style=` + `<style>` blocks only.
//!
//! Everything below the flag plumbing is pure and inline-unit-tested; `run` (one-shot dir
//! download) and `scrapeOne` (console /source-upload) add the I/O.
const std = @import("std");
const builtin = @import("builtin");
const net = @import("net.zig");
const image = @import("image.zig");
const pipeline = @import("pipeline.zig");
const args = @import("args.zig");
const logo = @import("logo.zig");

const MAX_HTML = 32 << 20; // sanity cap on a scraped page (fetch itself is unbounded)

// ── --source-name matcher (POSIX regex.h; substring fallback off-POSIX) ──────────
//
// The scrape name filter is a regex on the media URL. POSIX targets use the platform libc's
// regex.h via a small C shim (src/regex_shim.c — no new dependency, the CLI already links
// libc via libc++); Windows/WASI, which have no regex.h, fall back to a case-insensitive
// substring test. The shim owns the `regex_t` storage because Zig 0.16's translate-c renders
// glibc's `regex_t` as an opaque type that can't be embedded by value in a Zig struct. The
// `extern`s are only referenced under the comptime guard, so they don't link off-POSIX.
const has_posix_regex = builtin.os.tag != .windows and builtin.os.tag != .wasi;
extern fn stencil_regex_compile(pattern: [*:0]const u8) ?*anyopaque;
extern fn stencil_regex_match(handle: ?*anyopaque, text: [*:0]const u8) c_int;
extern fn stencil_regex_free(handle: ?*anyopaque) void;

/// A compiled `--source-name` matcher. POSIX: a case-insensitive extended regex; elsewhere: a
/// case-insensitive substring test. An absent/empty pattern matches everything.
const NameMatcher = struct {
    active: bool = false,
    pattern: []const u8 = "",
    handle: ?*anyopaque = null, // POSIX: opaque regex_t owned by the C shim; null = inactive

    /// Compile `pattern`; error.BadNamePattern on an invalid regex. `arena` owns the scratch
    /// NUL-terminated copy handed to the shim's regcomp.
    fn init(pattern: ?[]const u8, arena: std.mem.Allocator) !NameMatcher {
        const p = pattern orelse return .{};
        if (p.len == 0) return .{};
        if (has_posix_regex) {
            const pz = try arena.dupeZ(u8, p);
            const handle = stencil_regex_compile(pz.ptr) orelse return error.BadNamePattern;
            return .{ .active = true, .pattern = p, .handle = handle };
        }
        return .{ .active = true, .pattern = p };
    }

    fn deinit(self: *NameMatcher) void {
        if (has_posix_regex and self.handle != null) {
            stencil_regex_free(self.handle);
            self.handle = null;
        }
    }

    /// Does `url` match? An inactive matcher passes everything. On POSIX a scratch NUL copy is
    /// made (regexec needs it); an OOM there fails closed (drops the item).
    fn matches(self: *NameMatcher, url: []const u8, arena: std.mem.Allocator) bool {
        if (!self.active) return true;
        if (has_posix_regex) {
            const uz = arena.dupeZ(u8, url) catch return false;
            return stencil_regex_match(self.handle, uz.ptr) != 0;
        }
        return indexOfPosCI(url, 0, self.pattern) != null;
    }
};

// ── media model ───────────────────────────────────────────────────────────────

pub const Kind = enum { img, bg, video, poster };

/// One extracted media item. `url` is the absolute http(s) URL (owned by the parse
/// allocator); `is_poster` promotes the item to the `poster` category regardless of kind.
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

// ── format derivation (port of extension formatOf + norm) ──────────────────────

/// Lowercase, normalized media "format" token for a URL / data: URI, written into `buf`;
/// "" when none. Exact port of the extension's `formatOf`+`norm` (jpeg→jpg, svg+xml→svg,
/// quicktime→mov). `buf` needs ~16 bytes (longest data: subtype is "quicktime").
pub fn formatOf(buf: []u8, url: []const u8) []const u8 {
    if (url.len == 0) return "";
    if (std.ascii.startsWithIgnoreCase(url, "data:")) {
        // data:(image|video)/<subtype>[;...]
        const rest = url["data:".len..];
        const sub = if (std.ascii.startsWithIgnoreCase(rest, "image/"))
            rest["image/".len..]
        else if (std.ascii.startsWithIgnoreCase(rest, "video/"))
            rest["video/".len..]
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
    if (ext.len < 2 or ext.len > 5) return "";
    for (ext) |c| if (!std.ascii.isAlphanumeric(c)) return "";
    return norm(buf, ext);
}

/// Lowercase `ext` into `buf`, then apply the SUBSTRING normalizations jpeg→jpg,
/// svg+xml→svg, quicktime→mov — matching the extension's chained `String.replace` and the
/// pystencil `.replace` port (so e.g. a `data:` subtype `x-jpeg` normalizes to `x-jpg`).
/// Every replacement shrinks, so the result always fits back into `buf`.
fn norm(buf: []u8, ext: []const u8) []const u8 {
    const n = @min(ext.len, buf.len);
    _ = std.ascii.lowerString(buf[0..n], ext[0..n]);
    var scratch: [64]u8 = undefined;
    var cur: []const u8 = buf[0..n];
    inline for (.{
        .{ "jpeg", "jpg" },
        .{ "svg+xml", "svg" },
        .{ "quicktime", "mov" },
    }) |pair| {
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
fn pathnameOf(url: []const u8) []const u8 {
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

// ── filters (port of filters.js) ───────────────────────────────────────────────

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

/// Inclusive width/height bound check (port of filters.js:81-88). Each bound applies only
/// when set; an unmeasured item (dims null) passes unconditionally; a measured axis is
/// rejected only when `< min` or `> max`.
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

// ── image dimension header-sniff ───────────────────────────────────────────────

pub const Sniff = struct { width: u32, height: u32, fmt: []const u8 };

fn u16be(b: []const u8, o: usize) u32 {
    return (@as(u32, b[o]) << 8) | b[o + 1];
}
fn u16le(b: []const u8, o: usize) u32 {
    return (@as(u32, b[o + 1]) << 8) | b[o];
}
fn u32be(b: []const u8, o: usize) u32 {
    return (@as(u32, b[o]) << 24) | (@as(u32, b[o + 1]) << 16) | (@as(u32, b[o + 2]) << 8) | b[o + 3];
}
fn u32le(b: []const u8, o: usize) u32 {
    return (@as(u32, b[o + 3]) << 24) | (@as(u32, b[o + 2]) << 16) | (@as(u32, b[o + 1]) << 8) | b[o];
}
fn u24le(b: []const u8, o: usize) u32 {
    return (@as(u32, b[o + 2]) << 16) | (@as(u32, b[o + 1]) << 8) | b[o];
}

const png_sig = [_]u8{ 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };

/// Sniff pixel dimensions + format from an image byte header (PNG / JPEG / GIF / BMP /
/// WebP). Returns null for anything it can't measure (e.g. video, SVG, truncated data).
pub fn sniff(b: []const u8) ?Sniff {
    // PNG: 8-byte signature, then a 4-byte length + "IHDR" + width/height (big-endian).
    if (b.len >= 24 and std.mem.eql(u8, b[0..8], &png_sig) and std.mem.eql(u8, b[12..16], "IHDR")) {
        return .{ .width = u32be(b, 16), .height = u32be(b, 20), .fmt = "png" };
    }
    // GIF: "GIF87a"/"GIF89a", then little-endian logical-screen width/height.
    if (b.len >= 10 and (std.mem.eql(u8, b[0..6], "GIF87a") or std.mem.eql(u8, b[0..6], "GIF89a"))) {
        return .{ .width = u16le(b, 6), .height = u16le(b, 8), .fmt = "gif" };
    }
    // BMP: "BM", then a little-endian (possibly negative) width/height in the DIB header.
    if (b.len >= 26 and b[0] == 'B' and b[1] == 'M') {
        const w: i32 = @bitCast(u32le(b, 18));
        const h: i32 = @bitCast(u32le(b, 22));
        return .{ .width = @abs(w), .height = @abs(h), .fmt = "bmp" };
    }
    // JPEG: FF D8, then walk segments to the first SOF marker.
    if (b.len >= 4 and b[0] == 0xFF and b[1] == 0xD8) {
        if (jpegDims(b)) |d| return d;
    }
    // WebP: RIFF....WEBP + a VP8 / VP8L / VP8X chunk.
    if (b.len >= 30 and std.mem.eql(u8, b[0..4], "RIFF") and std.mem.eql(u8, b[8..12], "WEBP")) {
        if (webpDims(b)) |d| return d;
    }
    return null;
}

fn jpegDims(b: []const u8) ?Sniff {
    var pos: usize = 2;
    while (pos + 9 <= b.len) {
        if (b[pos] != 0xFF) {
            pos += 1;
            continue;
        }
        const marker = b[pos + 1];
        // Standalone markers (no length payload): padding, RSTn, SOI/EOI, TEM.
        if (marker == 0xFF) {
            pos += 1;
            continue;
        }
        if (marker == 0x01 or (marker >= 0xD0 and marker <= 0xD9)) {
            pos += 2;
            continue;
        }
        // SOF0..SOF15 except the non-SOF C4 (DHT), C8 (JPG), CC (DAC).
        if (marker >= 0xC0 and marker <= 0xCF and marker != 0xC4 and marker != 0xC8 and marker != 0xCC) {
            return .{ .height = u16be(b, pos + 5), .width = u16be(b, pos + 7), .fmt = "jpg" };
        }
        const seg = u16be(b, pos + 2);
        if (seg < 2) return null;
        pos += 2 + seg;
    }
    return null;
}

fn webpDims(b: []const u8) ?Sniff {
    const tag = b[12..16];
    if (std.mem.eql(u8, tag, "VP8 ")) {
        // Lossy: 3-byte frame tag, the 3-byte start code 9D 01 2A, then 14-bit dims.
        return .{
            .width = (u16le(b, 26)) & 0x3FFF,
            .height = (u16le(b, 28)) & 0x3FFF,
            .fmt = "webp",
        };
    }
    if (std.mem.eql(u8, tag, "VP8L") and b.len >= 25) {
        // Lossless: 1-byte signature (0x2F), then packed 14-bit width-1/height-1.
        const b1 = b[21];
        const b2 = b[22];
        const b3 = b[23];
        const b4 = b[24];
        const w = 1 + (((@as(u32, b2) & 0x3F) << 8) | b1);
        const h = 1 + (((@as(u32, b4) & 0x0F) << 10) | (@as(u32, b3) << 2) | ((@as(u32, b2) & 0xC0) >> 6));
        return .{ .width = w, .height = h, .fmt = "webp" };
    }
    if (std.mem.eql(u8, tag, "VP8X")) {
        // Extended: 4-byte flags/reserved, then 24-bit width-1 / height-1 (little-endian).
        return .{ .width = 1 + u24le(b, 24), .height = 1 + u24le(b, 27), .fmt = "webp" };
    }
    return null;
}

// ── HTML parsing ───────────────────────────────────────────────────────────────

const RawImg = struct { url: []const u8, alt: []const u8 };
const RawVideo = struct { url: []const u8, poster: []const u8, alt: []const u8 };

/// Parse `html`, extracting media URLs resolved absolute against `base_url` (honoring a
/// `<base href>`), deduped first-wins, in the scan order: <img>, <svg><image>, <video>
/// (+ poster), <picture><source>, then CSS `url(...)` backgrounds. All strings are owned by
/// `alloc`. Only http(s) URLs are kept.
pub fn parseMedia(alloc: std.mem.Allocator, html: []const u8, base_url: []const u8) ![]Media {
    var imgs: std.ArrayList(RawImg) = .empty;
    defer imgs.deinit(alloc);
    var svgs: std.ArrayList(RawImg) = .empty;
    defer svgs.deinit(alloc);
    var videos: std.ArrayList(RawVideo) = .empty;
    defer videos.deinit(alloc);
    var pics: std.ArrayList([]const u8) = .empty;
    defer pics.deinit(alloc);
    var bgs: std.ArrayList([]const u8) = .empty;
    defer bgs.deinit(alloc);

    var base: []const u8 = base_url;
    var base_set = false;
    var in_video = false;
    var vid_idx: usize = 0;
    var in_picture = false;

    var i: usize = 0;
    while (i < html.len) {
        if (html[i] != '<') {
            i += 1;
            continue;
        }
        // Comments / doctype / processing instructions.
        if (std.mem.startsWith(u8, html[i..], "<!--")) {
            const end = std.mem.indexOfPos(u8, html, i + 4, "-->") orelse html.len;
            i = @min(end + 3, html.len);
            continue;
        }
        if (i + 1 < html.len and (html[i + 1] == '!' or html[i + 1] == '?')) {
            i = (std.mem.indexOfScalarPos(u8, html, i, '>') orelse (html.len - 1)) + 1;
            continue;
        }
        // Read to the matching '>' (respecting quoted attribute values).
        var j = i + 1;
        var quote: u8 = 0;
        while (j < html.len) : (j += 1) {
            const c = html[j];
            if (quote != 0) {
                if (c == quote) quote = 0;
            } else if (c == '"' or c == '\'') {
                quote = c;
            } else if (c == '>') break;
        }
        const tag = html[i + 1 .. @min(j, html.len)];
        i = @min(j + 1, html.len);
        if (tag.len == 0) continue;

        const is_close = tag[0] == '/';
        const name_start: usize = if (is_close) 1 else 0;
        var k = name_start;
        while (k < tag.len and !isSpace(tag[k]) and tag[k] != '/') : (k += 1) {}
        const name = tag[name_start..k];
        const body = tag[k..];

        if (is_close) {
            if (eqlCI(name, "video")) in_video = false;
            if (eqlCI(name, "picture")) in_picture = false;
            continue;
        }

        // <style>/<script>: skip the raw content (don't parse tags inside); harvest CSS
        // url(...) from a <style> block. Void/self-closed forms have no content.
        if (eqlCI(name, "style") or eqlCI(name, "script")) {
            const close = if (eqlCI(name, "style")) "</style" else "</script";
            const content_end = indexOfPosCI(html, i, close) orelse html.len;
            if (eqlCI(name, "style")) try extractCssUrls(alloc, &bgs, html[i..content_end]);
            i = if (content_end < html.len)
                (std.mem.indexOfScalarPos(u8, html, content_end, '>') orelse (html.len - 1)) + 1
            else
                html.len;
            continue;
        }

        // Any element may carry an inline background via style="".
        if (getAttr(body, "style")) |st| try extractCssUrls(alloc, &bgs, st);

        if (eqlCI(name, "base")) {
            if (!base_set) {
                if (getAttr(body, "href")) |href| {
                    if (try resolveUrl(alloc, base_url, href)) |abs| {
                        base = abs;
                        base_set = true;
                    }
                }
            }
        } else if (eqlCI(name, "img")) {
            try imgs.append(alloc, .{ .url = pickImgUrl(body), .alt = getAttr(body, "alt") orelse "" });
        } else if (eqlCI(name, "image")) {
            try svgs.append(alloc, .{ .url = getAttr(body, "href") orelse getAttr(body, "xlink:href") orelse "", .alt = "" });
        } else if (eqlCI(name, "video")) {
            const src = getAttr(body, "src") orelse "";
            vid_idx = videos.items.len;
            in_video = true;
            try videos.append(alloc, .{
                .url = if (try resolvesHttp(alloc, base, src)) src else "",
                .poster = getAttr(body, "poster") orelse "",
                .alt = getAttr(body, "aria-label") orelse "",
            });
        } else if (eqlCI(name, "picture")) {
            in_picture = true;
        } else if (eqlCI(name, "source")) {
            const src = getAttr(body, "src") orelse "";
            if (in_video) {
                if (videos.items[vid_idx].url.len == 0 and try resolvesHttp(alloc, base, src)) videos.items[vid_idx].url = src;
            } else if (in_picture and src.len != 0) {
                try pics.append(alloc, src);
            }
        }
    }

    // Resolution + dedupe phase, in category order.
    var out: std.ArrayList(Media) = .empty;
    errdefer out.deinit(alloc);
    var seen = std.StringHashMap(usize).init(alloc);
    defer seen.deinit();

    for (imgs.items) |r| try addMedia(alloc, &out, &seen, base, r.url, .img, r.alt, false);
    for (svgs.items) |r| try addMedia(alloc, &out, &seen, base, r.url, .img, "", false);
    for (videos.items) |v| {
        if (v.url.len != 0) try addMedia(alloc, &out, &seen, base, v.url, .video, v.alt, false);
        if (v.poster.len != 0) try addMedia(alloc, &out, &seen, base, v.poster, .img, "", true);
    }
    for (pics.items) |r| try addMedia(alloc, &out, &seen, base, r, .img, "", false);
    for (bgs.items) |r| try addMedia(alloc, &out, &seen, base, r, .bg, "", false);

    return out.toOwnedSlice(alloc);
}

/// Resolve `raw` against `base`, keep only http(s), decode HTML entities in the alt text,
/// and append — deduping on the absolute URL (first wins). A duplicate that arrives tagged
/// `is_poster` just promotes the already-collected item to the poster category.
fn addMedia(
    alloc: std.mem.Allocator,
    out: *std.ArrayList(Media),
    seen: *std.StringHashMap(usize),
    base: []const u8,
    raw: []const u8,
    kind: Kind,
    alt: []const u8,
    is_poster: bool,
) !void {
    const abs = (try resolveUrl(alloc, base, raw)) orelse return;
    if (!net.isUrl(abs)) return; // drop data:/blob:/other schemes for download
    if (seen.get(abs)) |idx| {
        if (is_poster) out.items[idx].is_poster = true;
        return;
    }
    const alt_dec = if (alt.len == 0) "" else try decodeEntities(alloc, alt);
    try out.append(alloc, .{ .url = abs, .kind = kind, .alt = alt_dec, .is_poster = is_poster });
    try seen.put(abs, out.items.len - 1);
}

/// The <img> URL: `src` unless it is empty or a `data:` placeholder, in which case fall back
/// to the first non-empty lazy attribute / first `srcset` candidate (static-HTML adaptation).
fn pickImgUrl(body: []const u8) []const u8 {
    const src = getAttr(body, "src") orelse "";
    if (src.len != 0 and !std.ascii.startsWithIgnoreCase(src, "data:")) return src;
    const lazies = [_][]const u8{ "data-src", "data-original", "data-lazy-src" };
    for (lazies) |a| {
        if (getAttr(body, a)) |v| if (v.len != 0) return v;
    }
    if (getAttr(body, "srcset")) |ss| {
        const first = firstSrcset(ss);
        if (first.len != 0) return first;
    }
    return src;
}

/// First URL of a `srcset` (the token before the first whitespace / comma).
fn firstSrcset(ss: []const u8) []const u8 {
    const s = std.mem.trim(u8, ss, " \t\r\n");
    const end = std.mem.indexOfAny(u8, s, " \t\r\n,") orelse s.len;
    return s[0..end];
}

/// Append every `url(...)` target from a CSS fragment (inline style or <style> block),
/// skipping `data:image/svg...` placeholders (port of extension `extractCssUrls`).
fn extractCssUrls(alloc: std.mem.Allocator, out: *std.ArrayList([]const u8), css: []const u8) !void {
    var i: usize = 0;
    while (std.mem.indexOfPos(u8, css, i, "url(")) |p| {
        var s = p + 4;
        while (s < css.len and isSpace(css[s])) s += 1;
        var q: u8 = 0;
        if (s < css.len and (css[s] == '"' or css[s] == '\'')) {
            q = css[s];
            s += 1;
        }
        const start = s;
        while (s < css.len) : (s += 1) {
            const c = css[s];
            if (q != 0) {
                if (c == q) break;
            } else if (c == ')') break;
        }
        const raw = std.mem.trim(u8, css[start..s], " \t\r\n");
        i = @min(s + 1, css.len);
        if (raw.len == 0) continue;
        if (std.ascii.startsWithIgnoreCase(raw, "data:image/svg")) continue;
        try out.append(alloc, raw);
    }
}

// ── URL resolution ─────────────────────────────────────────────────────────────

/// Resolve `raw` against absolute `base`, returning an owned absolute URL (or null when
/// empty / a same-document fragment). Handles scheme-absolute, protocol-relative (`//host`),
/// root-relative (`/path`), query-only (`?q`) and path-relative forms.
pub fn resolveUrl(alloc: std.mem.Allocator, base: []const u8, raw: []const u8) !?[]const u8 {
    const decoded = try decodeEntities(alloc, std.mem.trim(u8, raw, " \t\r\n"));
    const r = decoded;
    if (r.len == 0) return null;
    if (r[0] == '#') return null; // same document
    if (hasScheme(r)) return r; // already absolute (http/data/blob/…)
    if (std.mem.startsWith(u8, r, "//")) {
        const scheme = schemeOf(base) orelse "http";
        return try std.fmt.allocPrint(alloc, "{s}:{s}", .{ scheme, r });
    }
    if (r[0] == '/') {
        return try std.fmt.allocPrint(alloc, "{s}{s}", .{ originOf(base), r });
    }
    if (r[0] == '?') {
        return try std.fmt.allocPrint(alloc, "{s}{s}", .{ baseNoQuery(base), r });
    }
    // Path-relative: join against the base "directory". dirOf yields the origin (no trailing
    // slash) when the base has no path, so insert a separator in that case.
    const dir = dirOf(base);
    const sep: []const u8 = if (dir.len != 0 and dir[dir.len - 1] == '/') "" else "/";
    return try std.fmt.allocPrint(alloc, "{s}{s}{s}", .{ dir, sep, r });
}

/// True when `raw`, RESOLVED against `base`, is an http(s) URL. Used to gate `<video src>`
/// and in-`<video>` `<source src>`: a relative/protocol-relative src must be resolved to an
/// absolute URL FIRST (the extension reads the DOM-resolved absolute), then scheme-checked —
/// matching pystencil. Resolution scratch is thrown away (arena); the caller keeps the raw
/// src, which the dedupe phase re-resolves against the final base.
fn resolvesHttp(alloc: std.mem.Allocator, base: []const u8, raw: []const u8) !bool {
    if (raw.len == 0) return false;
    const abs = (try resolveUrl(alloc, base, raw)) orelse return false;
    return net.isUrl(abs);
}

/// True when `s` starts with a URL scheme (`scheme:`), e.g. `http:`, `data:`, `blob:`.
fn hasScheme(s: []const u8) bool {
    if (s.len == 0 or !std.ascii.isAlphabetic(s[0])) return false;
    for (s, 0..) |c, idx| {
        if (idx == 0) continue;
        if (c == ':') return true;
        if (!(std.ascii.isAlphanumeric(c) or c == '+' or c == '-' or c == '.')) return false;
    }
    return false;
}

fn schemeOf(url: []const u8) ?[]const u8 {
    const c = std.mem.indexOfScalar(u8, url, ':') orelse return null;
    return url[0..c];
}

/// `scheme://host[:port]` of an absolute URL (no trailing path).
fn originOf(url: []const u8) []const u8 {
    const s = std.mem.indexOf(u8, url, "://") orelse return url;
    const after = s + 3;
    const slash = std.mem.indexOfScalarPos(u8, url, after, '/') orelse url.len;
    return url[0..slash];
}

fn baseNoQuery(url: []const u8) []const u8 {
    const q = std.mem.indexOfAny(u8, url, "?#") orelse return url;
    return url[0..q];
}

/// The "directory" of a base URL: everything up to and including its last path `/`
/// (falling back to the origin + `/` when there is no path).
fn dirOf(url: []const u8) []const u8 {
    const clean = baseNoQuery(url);
    const origin = originOf(clean);
    if (clean.len == origin.len) return url[0 .. origin.len]; // no path → caller joins raw after
    const slash = std.mem.lastIndexOfScalar(u8, clean, '/') orelse return clean;
    if (slash < origin.len) return clean[0..origin.len];
    return clean[0 .. slash + 1];
}

// ── small HTML helpers ─────────────────────────────────────────────────────────

fn isSpace(c: u8) bool {
    return c == ' ' or c == '\t' or c == '\r' or c == '\n' or c == '\x0c';
}

fn eqlCI(a: []const u8, b: []const u8) bool {
    return std.ascii.eqlIgnoreCase(a, b);
}

fn indexOfPosCI(hay: []const u8, start: usize, needle: []const u8) ?usize {
    if (needle.len == 0 or needle.len > hay.len) return null;
    var i = start;
    while (i + needle.len <= hay.len) : (i += 1) {
        if (std.ascii.eqlIgnoreCase(hay[i .. i + needle.len], needle)) return i;
    }
    return null;
}

/// Case-insensitive attribute lookup over a tag body (the text after the element name).
/// Returns the raw (still entity-encoded) value, or null when absent.
fn getAttr(body: []const u8, name: []const u8) ?[]const u8 {
    var i: usize = 0;
    while (i < body.len) {
        while (i < body.len and (isSpace(body[i]) or body[i] == '/')) i += 1;
        const start = i;
        while (i < body.len and !isSpace(body[i]) and body[i] != '=' and body[i] != '/' and body[i] != '>') i += 1;
        const attr = body[start..i];
        while (i < body.len and isSpace(body[i])) i += 1;
        var val: []const u8 = "";
        if (i < body.len and body[i] == '=') {
            i += 1;
            while (i < body.len and isSpace(body[i])) i += 1;
            if (i < body.len and (body[i] == '"' or body[i] == '\'')) {
                const q = body[i];
                i += 1;
                const vs = i;
                while (i < body.len and body[i] != q) i += 1;
                val = body[vs..i];
                if (i < body.len) i += 1;
            } else {
                const vs = i;
                while (i < body.len and !isSpace(body[i]) and body[i] != '>') i += 1;
                val = body[vs..i];
            }
        }
        if (attr.len != 0 and eqlCI(attr, name)) return val;
        if (attr.len == 0 and val.len == 0) i += 1; // guard against no progress
    }
    return null;
}

/// Decode the handful of HTML entities that appear in URLs / alt text into an owned copy.
fn decodeEntities(alloc: std.mem.Allocator, s: []const u8) ![]u8 {
    var out: std.ArrayList(u8) = .empty;
    errdefer out.deinit(alloc);
    var i: usize = 0;
    while (i < s.len) {
        if (s[i] == '&') {
            const ents = [_]struct { k: []const u8, v: u8 }{
                .{ .k = "&amp;", .v = '&' },
                .{ .k = "&#38;", .v = '&' },
                .{ .k = "&lt;", .v = '<' },
                .{ .k = "&gt;", .v = '>' },
                .{ .k = "&quot;", .v = '"' },
                .{ .k = "&#39;", .v = '\'' },
                .{ .k = "&apos;", .v = '\'' },
            };
            var matched = false;
            for (ents) |e| {
                if (std.mem.startsWith(u8, s[i..], e.k)) {
                    try out.append(alloc, e.v);
                    i += e.k.len;
                    matched = true;
                    break;
                }
            }
            if (matched) continue;
        }
        try out.append(alloc, s[i]);
        i += 1;
    }
    return out.toOwnedSlice(alloc);
}

// ── filename derivation ─────────────────────────────────────────────────────────

/// A safe download filename for `url` (sanitized last path segment, extension ensured from
/// `ext`), or `source-{index}.{ext}` when the segment is empty. Owned by `alloc`.
pub fn deriveName(alloc: std.mem.Allocator, url: []const u8, ext: []const u8, index: usize) ![]u8 {
    const use_ext = if (ext.len != 0) ext else "bin";
    const path = pathnameOf(url);
    const slash = std.mem.lastIndexOfScalar(u8, path, '/');
    const seg = if (slash) |s| path[s + 1 ..] else path;

    var san: std.ArrayList(u8) = .empty;
    defer san.deinit(alloc);
    for (seg) |c| {
        if (std.ascii.isAlphanumeric(c) or c == '.' or c == '_' or c == '-') {
            try san.append(alloc, c);
        } else {
            try san.append(alloc, '_');
        }
    }
    // Strip leading dots so a segment like ".htaccess" / "." can't hide the name or escape.
    const name = std.mem.trimStart(u8, san.items, ".");
    if (name.len == 0) return std.fmt.allocPrint(alloc, "source-{d}.{s}", .{ index, use_ext });

    // Ensure a plausible extension; if the segment has none, append the derived one.
    if (!hasNameExt(name)) {
        return std.fmt.allocPrint(alloc, "{s}.{s}", .{ name, use_ext });
    }
    return alloc.dupe(u8, name);
}

/// True when `name` already ends in a 2–5 char alphanumeric extension.
fn hasNameExt(name: []const u8) bool {
    const dot = std.mem.lastIndexOfScalar(u8, name, '.') orelse return false;
    const ext = name[dot + 1 ..];
    if (ext.len < 2 or ext.len > 5) return false;
    for (ext) |c| if (!std.ascii.isAlphanumeric(c)) return false;
    return true;
}

/// Join an output directory and a filename into a path (no doubled slash; `.`/empty → bare).
fn joinPath(alloc: std.mem.Allocator, dir: []const u8, name: []const u8) ![]u8 {
    if (dir.len == 0 or std.mem.eql(u8, dir, ".")) return alloc.dupe(u8, name);
    const sep: []const u8 = if (dir[dir.len - 1] == '/') "" else "/";
    return std.fmt.allocPrint(alloc, "{s}{s}{s}", .{ dir, sep, name });
}

// ── one-shot run (scrape mode) ──────────────────────────────────────────────────

/// Normalize a CLI `0 = unset` bound to the optional the filter uses.
fn boundOpt(v: u32) ?u32 {
    return if (v == 0) null else v;
}

/// Whether a media sub-resource fetch must run in strict mode (loopback blocked). Loopback /
/// internal targets are tolerated for a media URL ONLY when it is on the SAME host the user
/// named (the page) — so scraping your own `localhost` gallery still works, while a public
/// page that smuggles `<img src="http://127.0.0.1/…">` (a DIFFERENT internal host) is refused.
/// An unparseable media host errs safe (strict). Private/link-local/metadata stay blocked in
/// both modes; this toggle only governs loopback. `page_host` is the page URL's bare host.
fn subStrict(media_url: []const u8, page_host: []const u8) bool {
    const mh = net.hostOf(media_url) orelse return true;
    return !std.ascii.eqlIgnoreCase(mh, page_host);
}

/// Injectable I/O seam so `runImpl`'s orchestration (fetch → filter → window → write → the
/// §3 stderr lines) is unit-testable with no network and no disk. `run` wires the real
/// net.fetch / logo.print / cwd filesystem; the test wires in-memory fakes. Everything the
/// scrape loop touches outside the pure helpers goes through here.
pub const Deps = struct {
    ctx: *anyopaque,
    fetchFn: *const fn (*anyopaque, std.mem.Allocator, std.Io, []const u8, bool) anyerror![]u8,
    emitFn: *const fn (*anyopaque, []const u8) void,
    mkdirFn: *const fn (*anyopaque, std.Io, []const u8) anyerror!void,
    writeFn: *const fn (*anyopaque, std.Io, []const u8, []const u8) anyerror!void,

    /// `strict` blocks loopback too: pass false for the user-named page URL, true for the
    /// media sub-resource URLs harvested from that (untrusted) page's content.
    fn fetch(self: Deps, a: std.mem.Allocator, io: std.Io, url: []const u8, strict: bool) ![]u8 {
        return self.fetchFn(self.ctx, a, io, url, strict);
    }
    /// Format one stderr line into `arena` and hand it to the sink (a no-op on OOM).
    fn emit(self: Deps, arena: std.mem.Allocator, comptime fmt: []const u8, a: anytype) void {
        const s = std.fmt.allocPrint(arena, fmt, a) catch return;
        self.emitFn(self.ctx, s);
    }
    fn mkdir(self: Deps, io: std.Io, path: []const u8) !void {
        return self.mkdirFn(self.ctx, io, path);
    }
    fn write(self: Deps, io: std.Io, path: []const u8, data: []const u8) !void {
        return self.writeFn(self.ctx, io, path, data);
    }
};

fn realFetch(_: *anyopaque, a: std.mem.Allocator, io: std.Io, url: []const u8, strict: bool) anyerror![]u8 {
    return net.fetch(a, io, url, strict);
}
fn realEmit(_: *anyopaque, s: []const u8) void {
    logo.print("{s}", .{s});
}
fn realMkdir(_: *anyopaque, io: std.Io, path: []const u8) anyerror!void {
    return std.Io.Dir.cwd().createDirPath(io, path);
}
fn realWrite(_: *anyopaque, io: std.Io, path: []const u8, data: []const u8) anyerror!void {
    return std.Io.Dir.cwd().writeFile(io, .{ .sub_path = path, .data = data });
}

/// Scrape `opts.source_site`, filter, download the matching window into `opts.output`, and
/// print the §3 stderr lines. Per-item fetch failures are non-fatal; zero files written is a
/// hard error (exit 1).
pub fn run(gpa: std.mem.Allocator, io: std.Io, opts: args.Options) !void {
    var unused: u8 = 0;
    const deps = Deps{
        .ctx = @ptrCast(&unused),
        .fetchFn = realFetch,
        .emitFn = realEmit,
        .mkdirFn = realMkdir,
        .writeFn = realWrite,
    };
    return runImpl(gpa, io, opts, deps);
}

fn runImpl(gpa: std.mem.Allocator, io: std.Io, opts: args.Options, deps: Deps) !void {
    var arena_state = std.heap.ArenaAllocator.init(gpa);
    defer arena_state.deinit();
    const arena = arena_state.allocator();

    const site = opts.source_site.?;
    const host_raw = net.hostOf(site) orelse {
        deps.emit(arena, "error: could not parse a host from URL '{s}'\n", .{site});
        return error.BadSourceUrl;
    };
    const dir = opts.output orelse ".";
    if (pipeline.hasParentTraversal(dir)) {
        deps.emit(arena, "error: refusing to write to a path that escapes the working directory: '{s}'\n", .{dir});
        return error.UnsafeOutputPath;
    }

    // Lowercase the host LOCALLY for the output lines only (parity with pystencil's
    // urlparse().hostname, which is already lowercase); net.hostOf stays case-preserving.
    const host = try std.ascii.allocLowerString(arena, host_raw);

    // Compile the optional --source-name regex before any I/O so a bad pattern fails fast.
    var name_matcher = NameMatcher.init(opts.source_name, arena) catch {
        deps.emit(arena, "error: invalid --source-name regex '{s}'\n", .{opts.source_name.?});
        return error.BadNamePattern;
    };
    defer name_matcher.deinit();

    // Announce the scrape up front: the page fetch and the per-item downloads can take a while,
    // so emit a progress line before any network I/O rather than sitting silent until the first
    // `wrote`. It carries none of the parsed prefixes (`wrote `/`scraped `/`error:`), so the mcp
    // and bot adapters ignore it; pystencil's _run_scrape mirrors it.
    deps.emit(arena, "scraping {s}…\n", .{site});

    const html = try deps.fetch(arena, io, site, false); // net prints its own error on failure
    if (html.len > MAX_HTML) {
        deps.emit(arena, "error: scraped page too large ({d} bytes)\n", .{html.len});
        return error.PageTooLarge;
    }

    const medias = try parseMedia(arena, html, site);

    const filter = opts.source_filter orelse "all";
    const formats = opts.source_format orelse "all";
    const min_w = boundOpt(opts.source_min_width);
    const max_w = boundOpt(opts.source_max_width);
    const min_h = boundOpt(opts.source_min_height);
    const max_h = boundOpt(opts.source_max_height);
    const dim_active = min_w != null or max_w != null or min_h != null or max_h != null;

    // Category + format filter (cheap, no network).
    var candidates: std.ArrayList(Media) = .empty;
    defer candidates.deinit(arena);
    for (medias) |m| {
        if (categoryPass(m, filter) and formatPass(m, formats) and name_matcher.matches(m.url, arena))
            try candidates.append(arena, m);
    }

    // A candidate ready to write. `bytes == null` means measurement couldn't fetch it: it
    // still occupies its window slot (unknown size passes the dimension filter, matching
    // pystencil), and the write loop re-fetches it — emitting the non-fatal per-item error if
    // that fails too, exactly as pystencil's separate download pass does.
    const Ready = struct { media: Media, bytes: ?[]const u8, dims: ?Sniff };
    var ready: std.ArrayList(Ready) = .empty;
    defer ready.deinit(arena);

    if (dim_active) {
        // Measure before slicing: fetch every candidate, keep the dimension-passers, then window.
        // A fetch failure here is silent (parity with pystencil's best-effort _measure_item):
        // the item stays as unknown-size (dims null) so it keeps its place in the window.
        var passed: std.ArrayList(Ready) = .empty;
        defer passed.deinit(arena);
        for (candidates.items) |m| {
            // These URLs came from the fetched page's content: loopback allowed only if the
            // media is on the same host the user named (subStrict), else blocked.
            const bytes = deps.fetch(arena, io, m.url, subStrict(m.url, host)) catch {
                try passed.append(arena, .{ .media = m, .bytes = null, .dims = null });
                continue;
            };
            const dims = sniff(bytes);
            if (dimensionPass(dims, min_w, max_w, min_h, max_h)) {
                try passed.append(arena, .{ .media = m, .bytes = bytes, .dims = dims });
            }
        }
        const win = window(Ready, passed.items, opts.group, effectiveCount(opts.source_count));
        for (win) |it| try ready.append(arena, it);
    } else {
        // No measurement needed: slice first, fetch only the window.
        const win = window(Media, candidates.items, opts.group, effectiveCount(opts.source_count));
        for (win) |m| {
            const bytes = deps.fetch(arena, io, m.url, subStrict(m.url, host)) catch |e| {
                deps.emit(arena, "error: could not fetch {s} ({s})\n", .{ m.url, @errorName(e) });
                continue;
            };
            try ready.append(arena, .{ .media = m, .bytes = bytes, .dims = sniff(bytes) });
        }
    }

    if (ready.items.len != 0 and !std.mem.eql(u8, dir, ".")) {
        deps.mkdir(io, dir) catch |e| {
            deps.emit(arena, "error: could not create output directory '{s}' ({s})\n", .{ dir, @errorName(e) });
            return e;
        };
    }

    var used = std.StringHashMap(void).init(gpa);
    defer used.deinit();
    var written: usize = 0;
    for (ready.items, 0..) |it, idx| {
        // Re-fetch items whose measurement fetch failed (bytes == null); a second failure is
        // the non-fatal per-item error, matching pystencil's download pass.
        var dims = it.dims;
        const bytes = it.bytes orelse blk: {
            const b = deps.fetch(arena, io, it.media.url, subStrict(it.media.url, host)) catch |e| {
                deps.emit(arena, "error: could not fetch {s} ({s})\n", .{ it.media.url, @errorName(e) });
                continue;
            };
            dims = sniff(b);
            break :blk b;
        };
        var fbuf: [16]u8 = undefined;
        const ext = formatFor(&fbuf, it.media, dims);
        var name = try deriveName(arena, it.media.url, ext, idx);
        if (used.contains(name)) name = try std.fmt.allocPrint(arena, "source-{d}.{s}", .{ idx, ext });
        try used.put(name, {});
        if (pipeline.hasParentTraversal(name)) continue; // sanitized names never do; belt-and-braces
        const path = try joinPath(arena, dir, name);
        deps.write(io, path, bytes) catch |e| {
            deps.emit(arena, "error: could not write {s} ({s})\n", .{ path, @errorName(e) });
            continue;
        };
        if (dims) |d| {
            deps.emit(arena, "wrote {s} ({d}x{d} px · source {s})\n", .{ path, d.width, d.height, host });
        } else {
            deps.emit(arena, "wrote {s} (source {s})\n", .{ path, host });
        }
        written += 1;
    }

    if (written == 0) {
        deps.emit(arena, "error: no media matched at {s}\n", .{site});
        return error.NoMediaMatched;
    }
    deps.emit(arena, "scraped {d} file(s) from {s} into {s}\n", .{ written, host, dir });
}

/// The download extension: prefer the URL's format token, fall back to the content sniff.
fn formatFor(buf: []u8, m: Media, dims: ?Sniff) []const u8 {
    const f = formatOf(buf, m.url);
    if (f.len != 0) return f;
    if (dims) |d| return d.fmt;
    return "";
}

/// Map the user-facing `--source-count` to the low-level `window` count: absent (null) picks
/// the default of 5; an explicit `0` means "all" (null); any N passes through unchanged. Only
/// the CLI entry layer applies this — `window` itself keeps null = all.
fn effectiveCount(opt: ?u32) ?u32 {
    const n = opt orelse return 5;
    return if (n == 0) null else n;
}

/// The group/count window over `items`: all of it when `count` is null, else
/// `items[group*count .. group*count+count]` (clamped).
fn window(comptime T: type, items: []T, group: u32, count: ?u32) []T {
    const n = count orelse return items;
    if (n == 0) return items[0..0];
    const start = @min(@as(usize, group) * n, items.len);
    const end = @min(start + n, items.len);
    return items[start..end];
}

// ── console /source-upload ──────────────────────────────────────────────────────

pub const ConsoleOpts = struct {
    url: []const u8,
    index: u32 = 0,
    format: []const u8 = "all",
    min_width: ?u32 = null,
    max_width: ?u32 = null,
    min_height: ?u32 = null,
    max_height: ?u32 = null,
    /// Optional custom label for the loaded image ("" = derive from the media URL). Consumed
    /// by the console handler (doSourceUpload), not by scrapeOne — it does not affect scraping.
    name: []const u8 = "",
};

/// Result of a console scrape: an owned decoded image, the chosen media URL (owned), and the
/// format to save it back as. Caller loads it into the session and frees `url`.
pub const Loaded = struct { img: image.Rgba8, url: []u8, fmt: image.Format };

/// Scrape `o.url`, filter to image-category items (img / bg / poster) by format + dimension,
/// pick the item at 0-based `o.index` from the ordered filtered list, download + decode it.
/// Prints on failure and returns an error; the caller leaves the session unchanged then.
pub fn scrapeOne(gpa: std.mem.Allocator, io: std.Io, o: ConsoleOpts) !Loaded {
    var arena_state = std.heap.ArenaAllocator.init(gpa);
    defer arena_state.deinit();
    const arena = arena_state.allocator();

    const html = try net.fetch(gpa, io, o.url, false); // user-named page URL
    defer gpa.free(html);
    const medias = try parseMedia(arena, html, o.url);
    const page_host = net.hostOf(o.url) orelse ""; // for the sub-resource same-host rule

    const dim_active = o.min_width != null or o.max_width != null or o.min_height != null or o.max_height != null;

    // Walk the ordered image-category (img / background / poster — never video), format-passing
    // candidates. Only a dimension filter forces a per-candidate fetch (to measure each and
    // apply the size bound, mirroring pystencil's scan_page); without one the category+format
    // list already fixes the pick, so we index straight in and fetch ONLY the chosen item.
    var matches: usize = 0;
    for (medias) |m| {
        const cat = m.category();
        const is_image = std.mem.eql(u8, cat, "img") or std.mem.eql(u8, cat, "background") or std.mem.eql(u8, cat, "poster");
        if (!is_image) continue;
        if (!formatPass(m, o.format)) continue;

        var dims: ?Sniff = null;
        var cached: ?[]const u8 = null; // measurement bytes, reused for the pick (no double fetch)
        if (dim_active) {
            const bytes = net.fetch(arena, io, m.url, subStrict(m.url, page_host)) catch |e| {
                logo.print("error: could not fetch {s} ({s})\n", .{ m.url, @errorName(e) });
                continue;
            };
            cached = bytes;
            dims = sniff(bytes);
            if (!dimensionPass(dims, o.min_width, o.max_width, o.min_height, o.max_height)) continue;
        }

        if (matches == o.index) {
            const bytes = cached orelse (net.fetch(arena, io, m.url, subStrict(m.url, page_host)) catch |e| {
                logo.print("error: could not fetch {s} ({s})\n", .{ m.url, @errorName(e) });
                return e;
            });
            if (dims == null) dims = sniff(bytes);
            const img = image.decode(gpa, bytes) catch |e| {
                logo.print("error: could not decode an image from '{s}' ({s})\n", .{ m.url, @errorName(e) });
                return e;
            };
            var fbuf: [16]u8 = undefined;
            const ext = formatFor(&fbuf, m, dims);
            const fmt = image.formatFromExt(ext) orelse .png;
            return .{ .img = img, .url = try gpa.dupe(u8, m.url), .fmt = fmt };
        }
        matches += 1;
    }
    logo.print("error: no scrape match at index {d} for {s}\n", .{ o.index, o.url });
    return error.NoMediaMatched;
}

// ── tests ───────────────────────────────────────────────────────────────────────

const testing = std.testing;

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

test "sniff: PNG / GIF / BMP / JPEG / WebP headers" {
    // PNG 200x80.
    var png = [_]u8{ 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a, 0, 0, 0, 0x0d, 'I', 'H', 'D', 'R', 0, 0, 0, 200, 0, 0, 0, 80 };
    const ps = sniff(&png).?;
    try testing.expectEqual(@as(u32, 200), ps.width);
    try testing.expectEqual(@as(u32, 80), ps.height);
    try testing.expectEqualStrings("png", ps.fmt);

    // GIF 4x3 (little-endian).
    var gif = [_]u8{ 'G', 'I', 'F', '8', '9', 'a', 4, 0, 3, 0 };
    const gs = sniff(&gif).?;
    try testing.expectEqual(@as(u32, 4), gs.width);
    try testing.expectEqual(@as(u32, 3), gs.height);

    // BMP 10x20 (width @18, height @22, little-endian).
    var bmp = [_]u8{0} ** 26;
    bmp[0] = 'B';
    bmp[1] = 'M';
    bmp[18] = 10;
    bmp[22] = 20;
    const bs = sniff(&bmp).?;
    try testing.expectEqual(@as(u32, 10), bs.width);
    try testing.expectEqual(@as(u32, 20), bs.height);

    // JPEG 1280x720 via an SOF0 marker.
    var jpg = [_]u8{ 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x04, 0, 0, 0xFF, 0xC0, 0x00, 0x11, 0x08, 0x02, 0xD0, 0x05, 0x00 };
    const js = sniff(&jpg).?;
    try testing.expectEqual(@as(u32, 1280), js.width);
    try testing.expectEqual(@as(u32, 720), js.height);
    try testing.expectEqualStrings("jpg", js.fmt);

    // WebP (lossy VP8) 64x48.
    var webp = [_]u8{0} ** 30;
    @memcpy(webp[0..4], "RIFF");
    @memcpy(webp[8..12], "WEBP");
    @memcpy(webp[12..16], "VP8 ");
    webp[23] = 0x9d;
    webp[24] = 0x01;
    webp[25] = 0x2a;
    webp[26] = 64;
    webp[27] = 0;
    webp[28] = 48;
    webp[29] = 0;
    const ws = sniff(&webp).?;
    try testing.expectEqual(@as(u32, 64), ws.width);
    try testing.expectEqual(@as(u32, 48), ws.height);
    try testing.expectEqualStrings("webp", ws.fmt);

    try testing.expect(sniff("not an image") == null);
}

test "resolveUrl: absolute, protocol/root/path-relative" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    const base = "https://example.com/a/b/page.html";
    try testing.expectEqualStrings("https://cdn.test/x.png", (try resolveUrl(a, base, "https://cdn.test/x.png")).?);
    try testing.expectEqualStrings("https://cdn.test/x.png", (try resolveUrl(a, base, "//cdn.test/x.png")).?);
    try testing.expectEqualStrings("https://example.com/x.png", (try resolveUrl(a, base, "/x.png")).?);
    try testing.expectEqualStrings("https://example.com/a/b/c.png", (try resolveUrl(a, base, "c.png")).?);
    try testing.expectEqualStrings("https://example.com/x.png", (try resolveUrl(a, "https://example.com", "x.png")).?);
    try testing.expectEqualStrings("data:image/png;base64,AA", (try resolveUrl(a, base, "data:image/png;base64,AA")).?);
    try testing.expect((try resolveUrl(a, base, "#top")) == null);
    try testing.expect((try resolveUrl(a, base, "  ")) == null);
    // &amp; in a query is decoded.
    try testing.expectEqualStrings("https://example.com/a/b/i.png?x=1&y=2", (try resolveUrl(a, base, "i.png?x=1&amp;y=2")).?);
}

test "deriveName: segment, extension ensure, fallback" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    try testing.expectEqualStrings("logo.png", try deriveName(a, "https://x/a/logo.png", "png", 0));
    try testing.expectEqualStrings("pic.png", try deriveName(a, "https://x/pic?v=2", "png", 3)); // no ext → append
    try testing.expectEqualStrings("source-5.jpg", try deriveName(a, "https://x/", "jpg", 5)); // empty segment
    try testing.expectEqualStrings("a_b.png", try deriveName(a, "https://x/a b.png", "png", 0)); // sanitized space
}

test "parseMedia: order, kinds, dedupe, lazy src, poster tag, background" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    const html =
        \\<html><head><base href="https://example.com/dir/"></head><body>
        \\<img src="logo.png" alt="Logo &amp; co">
        \\<img src="data:image/gif;base64,AAA" data-src="lazy.jpg">
        \\<svg><image xlink:href="/vec.svg"></image></svg>
        \\<picture><source src="hero.webp"><img src="hero.png"></picture>
        \\<video src="https://cdn.test/clip.mp4" poster="poster.png"></video>
        \\<div style="background-image:url('bg.jpg')"></div>
        \\<style>.x{background:url(https://example.com/dir/logo.png)}</style>
        \\</body></html>
    ;
    const items = try parseMedia(a, html, "https://example.com/page.html");
    // Scan order: imgs, svg image, video (+poster), picture source, backgrounds.
    // hero.png is a plain <img> inside <picture>; the picture <source> hero.webp is separate.
    try testing.expectEqualStrings("https://example.com/dir/logo.png", items[0].url);
    try testing.expectEqualStrings("Logo & co", items[0].alt);
    try testing.expect(items[0].kind == .img);
    try testing.expectEqualStrings("https://example.com/dir/lazy.jpg", items[1].url); // data: src → lazy fallback
    try testing.expectEqualStrings("https://example.com/dir/hero.png", items[2].url);
    try testing.expectEqualStrings("https://example.com/vec.svg", items[3].url);
    try testing.expect(items[3].kind == .img);
    try testing.expectEqualStrings("https://cdn.test/clip.mp4", items[4].url);
    try testing.expect(items[4].kind == .video);
    try testing.expectEqualStrings("https://example.com/dir/poster.png", items[5].url);
    try testing.expect(items[5].is_poster);
    try testing.expectEqualStrings("poster", items[5].category());
    try testing.expectEqualStrings("https://example.com/dir/hero.webp", items[6].url);
    try testing.expectEqualStrings("https://example.com/dir/bg.jpg", items[7].url);
    try testing.expect(items[7].kind == .bg);
    // logo.png in the <style> block dedupes against the first <img> (first wins).
    try testing.expectEqual(@as(usize, 8), items.len);
}

test "parseMedia: poster equal to an existing img just tags it" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    const html =
        \\<img src="https://x/same.png">
        \\<video src="https://x/v.mp4" poster="https://x/same.png"></video>
    ;
    const items = try parseMedia(a, html, "https://x/p");
    try testing.expectEqual(@as(usize, 2), items.len); // img + video only; poster tags the img
    try testing.expectEqualStrings("https://x/same.png", items[0].url);
    try testing.expect(items[0].is_poster);
    try testing.expect(items[1].kind == .video);
}

test "parseMedia: relative video src / source src resolve then pass the http(s) gate" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    // A relative <video src> must be resolved against the base FIRST, then scheme-checked, so
    // it IS emitted as a resolved absolute video item (parity with pystencil / the extension).
    {
        const html = "<video src=\"clip.mp4\"></video>";
        const items = try parseMedia(a, html, "https://example.com/dir/page.html");
        try testing.expectEqual(@as(usize, 1), items.len);
        try testing.expectEqualStrings("https://example.com/dir/clip.mp4", items[0].url);
        try testing.expect(items[0].kind == .video);
    }
    // Same for a relative in-<video> <source src> when the <video src> is absent.
    {
        const html = "<video><source src=\"movie.webm\"></video>";
        const items = try parseMedia(a, html, "https://example.com/dir/page.html");
        try testing.expectEqual(@as(usize, 1), items.len);
        try testing.expectEqualStrings("https://example.com/dir/movie.webm", items[0].url);
        try testing.expect(items[0].kind == .video);
    }
}

test "subStrict: loopback tolerated only for same-host sub-resources" {
    // Same host as the page → non-strict (a localhost gallery's own images stay fetchable).
    try testing.expect(!subStrict("http://127.0.0.1:8080/a.png", "127.0.0.1"));
    try testing.expect(!subStrict("http://localhost/img.png", "localhost"));
    try testing.expect(!subStrict("http://cdn.example.com/x.png", "cdn.example.com"));
    // Different host → strict (the SSRF pivot: a public page pointing at loopback/internal).
    try testing.expect(subStrict("http://127.0.0.1/admin", "evil.com"));
    try testing.expect(subStrict("http://169.254.169.254/meta", "site.com"));
    try testing.expect(subStrict("http://other.com/x.png", "site.com"));
    // Unparseable media host errs safe (strict).
    try testing.expect(subStrict("http:///only-path", "site.com"));
}

test "effectiveCount: default 5, 0 = all, N passthrough" {
    try testing.expectEqual(@as(?u32, 5), effectiveCount(null)); // absent → default 5
    try testing.expectEqual(@as(?u32, null), effectiveCount(0)); // 0 → all
    try testing.expectEqual(@as(?u32, 3), effectiveCount(3)); // N → N
}

test "window: all vs group/count slicing" {
    var xs = [_]u32{ 0, 1, 2, 3, 4 };
    try testing.expectEqual(@as(usize, 5), window(u32, &xs, 0, null).len); // count absent → all
    try testing.expectEqualSlices(u32, &.{ 0, 1 }, window(u32, &xs, 0, 2));
    try testing.expectEqualSlices(u32, &.{ 2, 3 }, window(u32, &xs, 1, 2));
    try testing.expectEqualSlices(u32, &.{4}, window(u32, &xs, 2, 2)); // clamped tail
    try testing.expectEqual(@as(usize, 0), window(u32, &xs, 5, 2).len); // past the end
}

// ── run() orchestration tests (in-memory fetch + capture; no network, no disk) ──────
//
// These lock the glue the pure helpers don't cover: filter → window → create dir → write
// files → emit the §3 stderr grammar (`wrote … (WxH px · source host)` / `(source host)` /
// `scraped N file(s) …` / `no media matched`). A `Deps` seam swaps net.fetch/logo.print/cwd
// for in-memory fakes, so this needs no server and touches no files.

/// A 24-byte PNG header (signature + IHDR) carrying `w`×`h` (both < 256) so `sniff` measures it.
fn mkPng(a: std.mem.Allocator, w: u8, h: u8) []const u8 {
    const b = a.alloc(u8, 24) catch unreachable;
    @memcpy(b[0..8], &png_sig);
    @memcpy(b[8..12], &[_]u8{ 0, 0, 0, 0x0d }); // IHDR length 13
    @memcpy(b[12..16], "IHDR");
    @memcpy(b[16..20], &[_]u8{ 0, 0, 0, w }); // width, big-endian
    @memcpy(b[20..24], &[_]u8{ 0, 0, 0, h }); // height, big-endian
    return b;
}

const FakeIo = struct {
    a: std.mem.Allocator,
    fetches: std.StringHashMap([]const u8),
    files: std.StringHashMap([]const u8),
    lines: std.ArrayListUnmanaged([]const u8) = .empty,
    dirs: std.ArrayListUnmanaged([]const u8) = .empty,

    fn init(a: std.mem.Allocator) FakeIo {
        return .{
            .a = a,
            .fetches = std.StringHashMap([]const u8).init(a),
            .files = std.StringHashMap([]const u8).init(a),
        };
    }
    fn serve(self: *FakeIo, url: []const u8, bytes: []const u8) !void {
        try self.fetches.put(url, bytes);
    }
    fn deps(self: *FakeIo) Deps {
        return .{ .ctx = @ptrCast(self), .fetchFn = fetchFn, .emitFn = emitFn, .mkdirFn = mkdirFn, .writeFn = writeFn };
    }
    fn line(self: *FakeIo, i: usize) []const u8 {
        return self.lines.items[i];
    }
    fn fetchFn(ptr: *anyopaque, a: std.mem.Allocator, _: std.Io, url: []const u8, _: bool) anyerror![]u8 {
        const self: *FakeIo = @ptrCast(@alignCast(ptr));
        const v = self.fetches.get(url) orelse return error.HttpFailed;
        return a.dupe(u8, v);
    }
    fn emitFn(ptr: *anyopaque, s: []const u8) void {
        const self: *FakeIo = @ptrCast(@alignCast(ptr));
        const dup = self.a.dupe(u8, s) catch return;
        self.lines.append(self.a, dup) catch {};
    }
    fn mkdirFn(ptr: *anyopaque, _: std.Io, path: []const u8) anyerror!void {
        const self: *FakeIo = @ptrCast(@alignCast(ptr));
        try self.dirs.append(self.a, try self.a.dupe(u8, path));
    }
    fn writeFn(ptr: *anyopaque, _: std.Io, path: []const u8, data: []const u8) anyerror!void {
        const self: *FakeIo = @ptrCast(@alignCast(ptr));
        try self.files.put(try self.a.dupe(u8, path), try self.a.dupe(u8, data));
    }
};

test "run: category+format filter, wrote grammar, dir, summary" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var fake = FakeIo.init(a);
    // Mixed-case host to exercise the output-line lowercasing; c.jpg is an img but not png.
    const html = "<img src=\"a.png\"><img src=\"b.png\"><img src=\"c.jpg\">" ++
        "<video src=\"http://cdn.test/clip.mp4\" poster=\"p.png\"></video>";
    try fake.serve("http://Example.com/", html);
    try fake.serve("http://Example.com/a.png", mkPng(a, 10, 20));
    try fake.serve("http://Example.com/b.png", mkPng(a, 30, 40));

    var opts = args.Options{};
    opts.source_site = "http://Example.com/";
    opts.output = "out";
    opts.source_filter = "img";
    opts.source_format = "png";
    try runImpl(testing.allocator, io, opts, fake.deps());

    try testing.expectEqual(@as(usize, 2), fake.files.count());
    try testing.expectEqualSlices(u8, mkPng(a, 10, 20), fake.files.get("out/a.png").?);
    try testing.expectEqualSlices(u8, mkPng(a, 30, 40), fake.files.get("out/b.png").?);
    try testing.expectEqual(@as(usize, 1), fake.dirs.items.len);
    try testing.expectEqualStrings("out", fake.dirs.items[0]);
    try testing.expectEqual(@as(usize, 4), fake.lines.items.len);
    try testing.expectEqualStrings("scraping http://Example.com/…\n", fake.line(0));
    try testing.expectEqualStrings("wrote out/a.png (10x20 px · source example.com)\n", fake.line(1));
    try testing.expectEqualStrings("wrote out/b.png (30x40 px · source example.com)\n", fake.line(2));
    try testing.expectEqualStrings("scraped 2 file(s) from example.com into out\n", fake.line(3));
}

test "run: all categories, count 0 = all, unmeasured video line" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var fake = FakeIo.init(a);
    const html = "<img src=\"a.png\"><video src=\"http://cdn.test/clip.mp4\" poster=\"p.png\"></video>";
    try fake.serve("http://example.com/", html);
    try fake.serve("http://example.com/a.png", mkPng(a, 10, 20));
    try fake.serve("http://cdn.test/clip.mp4", "not-an-image mp4 bytes");
    try fake.serve("http://example.com/p.png", mkPng(a, 5, 5));

    var opts = args.Options{};
    opts.source_site = "http://example.com/";
    opts.output = "media";
    opts.source_count = 0; // 0 = all
    try runImpl(testing.allocator, io, opts, fake.deps());

    // img, then video-before-poster; the video is unmeasured → the no-dims `(source …)` line.
    try testing.expectEqual(@as(usize, 3), fake.files.count());
    try testing.expectEqual(@as(usize, 5), fake.lines.items.len);
    try testing.expectEqualStrings("scraping http://example.com/…\n", fake.line(0));
    try testing.expectEqualStrings("wrote media/a.png (10x20 px · source example.com)\n", fake.line(1));
    try testing.expectEqualStrings("wrote media/clip.mp4 (source example.com)\n", fake.line(2));
    try testing.expectEqualStrings("wrote media/p.png (5x5 px · source example.com)\n", fake.line(3));
    try testing.expectEqualStrings("scraped 3 file(s) from example.com into media\n", fake.line(4));
}

test "run: group/count windows the filtered list, fetching only the window" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var fake = FakeIo.init(a);
    try fake.serve("http://example.com/", "<img src=\"a.png\"><img src=\"b.png\"><img src=\"c.png\">");
    try fake.serve("http://example.com/a.png", mkPng(a, 1, 1));
    try fake.serve("http://example.com/b.png", mkPng(a, 2, 2));
    try fake.serve("http://example.com/c.png", mkPng(a, 3, 3));

    var opts = args.Options{};
    opts.source_site = "http://example.com/";
    opts.output = "out";
    opts.source_count = 1;
    opts.group = 1; // window = filtered[1..2] = [b]
    try runImpl(testing.allocator, io, opts, fake.deps());

    try testing.expectEqual(@as(usize, 1), fake.files.count());
    try testing.expect(fake.files.get("out/b.png") != null);
    try testing.expect(fake.files.get("out/a.png") == null); // outside the window → never fetched
    try testing.expectEqualStrings("scraping http://example.com/…\n", fake.line(0));
    try testing.expectEqualStrings("wrote out/b.png (2x2 px · source example.com)\n", fake.line(1));
    try testing.expectEqualStrings("scraped 1 file(s) from example.com into out\n", fake.line(2));
}

test "run: no matching media is a hard error" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var fake = FakeIo.init(a);
    try fake.serve("http://example.com/", "<img src=\"a.png\">");
    try fake.serve("http://example.com/a.png", mkPng(a, 10, 20));

    var opts = args.Options{};
    opts.source_site = "http://example.com/";
    opts.output = "out";
    opts.source_filter = "video"; // page has no video → nothing matches
    try testing.expectError(error.NoMediaMatched, runImpl(testing.allocator, io, opts, fake.deps()));

    try testing.expectEqual(@as(usize, 0), fake.files.count());
    try testing.expectEqual(@as(usize, 2), fake.lines.items.len);
    try testing.expectEqualStrings("scraping http://example.com/…\n", fake.line(0));
    try testing.expectEqualStrings("error: no media matched at http://example.com/\n", fake.line(1));
}

test "run: --source-name filters candidates by URL (regex or substring)" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var fake = FakeIo.init(a);
    try fake.serve("http://example.com/", "<img src=\"cat.png\"><img src=\"dog.jpg\"><img src=\"cat2.png\">");
    try fake.serve("http://example.com/cat.png", mkPng(a, 1, 1));
    try fake.serve("http://example.com/cat2.png", mkPng(a, 2, 2));
    try fake.serve("http://example.com/dog.jpg", mkPng(a, 3, 3));

    var opts = args.Options{};
    opts.source_site = "http://example.com/";
    opts.output = "out";
    opts.source_count = 0; // all
    opts.source_name = "cat"; // both a valid regex and substring → keeps cat.png + cat2.png
    try runImpl(testing.allocator, io, opts, fake.deps());

    try testing.expectEqual(@as(usize, 2), fake.files.count());
    try testing.expect(fake.files.get("out/cat.png") != null);
    try testing.expect(fake.files.get("out/cat2.png") != null);
    try testing.expect(fake.files.get("out/dog.jpg") == null); // filtered out by name
}

test "run: --source-name honours regex metacharacters (POSIX)" {
    if (!has_posix_regex) return error.SkipZigTest;
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var fake = FakeIo.init(a);
    try fake.serve("http://example.com/", "<img src=\"cat.png\"><img src=\"dog.jpg\">");
    try fake.serve("http://example.com/cat.png", mkPng(a, 1, 1));
    try fake.serve("http://example.com/dog.jpg", mkPng(a, 3, 3));

    var opts = args.Options{};
    opts.source_site = "http://example.com/";
    opts.output = "out";
    opts.source_count = 0;
    opts.source_name = "\\.jpg$"; // anchored regex → only the .jpg
    try runImpl(testing.allocator, io, opts, fake.deps());

    try testing.expectEqual(@as(usize, 1), fake.files.count());
    try testing.expect(fake.files.get("out/dog.jpg") != null);
    try testing.expect(fake.files.get("out/cat.png") == null);
}

test "run: --source-name invalid regex is a hard error (POSIX)" {
    if (!has_posix_regex) return error.SkipZigTest;
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var fake = FakeIo.init(a);
    try fake.serve("http://example.com/", "<img src=\"cat.png\">");

    var opts = args.Options{};
    opts.source_site = "http://example.com/";
    opts.output = "out";
    opts.source_name = "cat("; // unbalanced paren → regcomp fails
    try testing.expectError(error.BadNamePattern, runImpl(testing.allocator, io, opts, fake.deps()));
    try testing.expectEqual(@as(usize, 0), fake.files.count());
}

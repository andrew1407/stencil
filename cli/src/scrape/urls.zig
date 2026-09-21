//! Turning a page's relative references into absolute http(s) URLs, and a URL into the
//! filename it is downloaded as. A reference that does not resolve to http(s) is dropped.
const std = @import("std");
const net = @import("../net.zig");
const image = @import("../media/image.zig");
const mediaTypes = @import("../media/types.zig");
const testing = std.testing;
const text = @import("text.zig");
const filter = @import("filter.zig");
const pathnameOf = filter.pathnameOf;
const decodeEntities = text.decodeEntities;

/// Resolve `raw` against absolute `base`, returning an owned absolute URL (null when empty or a
/// same-document fragment). Handles scheme-absolute, `//host`, `/path`, `?q` and path-relative forms.
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

/// True when `raw`, RESOLVED against `base`, is an http(s) URL — the gate on `<video src>` and in-
/// `<video>` `<source src>`: resolve FIRST, then scheme-check, as pystencil and the DOM read do.
pub fn resolvesHttp(alloc: std.mem.Allocator, base: []const u8, raw: []const u8) !bool {
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
    if (clean.len == origin.len) return url[0..origin.len]; // no path → caller joins raw after
    const slash = std.mem.lastIndexOfScalar(u8, clean, '/') orelse return clean;
    if (slash < origin.len) return clean[0..origin.len];
    return clean[0 .. slash + 1];
}

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
    if (!mediaTypes.extLenOk(ext.len)) return false;
    for (ext) |c| if (!std.ascii.isAlphanumeric(c)) return false;
    return true;
}

/// Join an output directory and a filename into a path (no doubled slash; `.`/empty → bare).
pub fn joinPath(alloc: std.mem.Allocator, dir: []const u8, name: []const u8) ![]u8 {
    if (dir.len == 0 or std.mem.eql(u8, dir, ".")) return alloc.dupe(u8, name);
    const sep: []const u8 = if (dir[dir.len - 1] == '/') "" else "/";
    return std.fmt.allocPrint(alloc, "{s}{s}{s}", .{ dir, sep, name });
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

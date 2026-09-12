//! The byte-level text helpers the page walker needs: case-insensitive search, one tag
//! attribute, and the HTML entities a `src`/`alt` can carry.
const std = @import("std");

pub fn isSpace(c: u8) bool {
    return c == ' ' or c == '\t' or c == '\r' or c == '\n' or c == '\x0c';
}

pub fn eqlCI(a: []const u8, b: []const u8) bool {
    return std.ascii.eqlIgnoreCase(a, b);
}

pub fn indexOfPosCI(hay: []const u8, start: usize, needle: []const u8) ?usize {
    if (needle.len == 0 or needle.len > hay.len) return null;
    var i = start;
    while (i + needle.len <= hay.len) : (i += 1) {
        if (std.ascii.eqlIgnoreCase(hay[i .. i + needle.len], needle)) return i;
    }
    return null;
}

/// Case-insensitive attribute lookup over a tag body (the text after the element name).
/// Returns the raw (still entity-encoded) value, or null when absent.
pub fn getAttr(body: []const u8, name: []const u8) ?[]const u8 {
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
pub fn decodeEntities(alloc: std.mem.Allocator, s: []const u8) ![]u8 {
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


/// Append every `url(...)` target from a CSS fragment (inline style or <style> block),
/// skipping `data:image/svg...` placeholders (port of extension `extractCssUrls`).
pub fn extractCssUrls(alloc: std.mem.Allocator, out: *std.ArrayList([]const u8), css: []const u8) !void {
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

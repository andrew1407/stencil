//! The launch URI's value reading (uri.zig): form-decoding the pairs, folding a key's spelling,
//! and reading a value as JS `Number(v)`, a flag or a coerced console setting — each the way
//! browser/js/core/launch/query.js reads it.
const std = @import("std");
const keys = @import("uriKeys.zig");
const options = @import("options.zig");

pub const Pair = struct { key: []const u8, value: []const u8 };

/// A blank side in px, capped as `--blank` caps it.
pub fn px(v: f64) u32 {
    return @intFromFloat(@round(@min(v, @as(f64, options.BLANK_MAX_PX))));
}

pub fn trimmed(v: []const u8) []const u8 {
    return std.mem.trim(u8, v, " \t\r\n");
}
pub fn lower(a: std.mem.Allocator, v: []const u8) ![]const u8 {
    return std.ascii.allocLowerString(a, trimmed(v));
}
/// A trimmed, non-empty value; null otherwise.
pub fn text(v: ?[]const u8) ?[]const u8 {
    const t = trimmed(v orelse return null);
    return if (t.len == 0) null else t;
}
/// JS `Number(v)`: a finite number, else `fallback`.
pub fn numberOr(v: ?[]const u8, fallback: ?f64) ?f64 {
    const t = text(v) orelse return fallback;
    const n = std.fmt.parseFloat(f64, t) catch return fallback;
    return if (std.math.isFinite(n)) n else fallback;
}
pub fn sizeOr(v: ?[]const u8) ?f64 {
    const n = numberOr(v, null) orelse return null;
    return if (n > 0) n else null;
}
pub fn isHttp(s: []const u8) bool {
    return std.ascii.startsWithIgnoreCase(s, "http://") or std.ascii.startsWithIgnoreCase(s, "https://");
}
/// A bare flag, a 1 or a true word is on.
pub fn flag(a: std.mem.Allocator, v: []const u8) !bool {
    const s = try lower(a, v);
    return s.len == 0 or std.mem.eql(u8, s, "1") or keys.oneOf(s, &keys.true_words);
}

/// `mainColor`, `main_color` and `main-color` are one key.
pub fn kebab(a: std.mem.Allocator, k: []const u8) ![]const u8 {
    var out: std.ArrayList(u8) = .empty;
    var prev: u8 = 0;
    for (trimmed(k)) |ch| {
        const c: u8 = if (ch == '_') '-' else ch;
        if (std.ascii.isUpper(c) and (std.ascii.isLower(prev) or std.ascii.isDigit(prev))) try out.append(a, '-');
        try out.append(a, std.ascii.toLower(c));
        prev = c;
    }
    return std.mem.trim(u8, out.items, "-");
}
pub fn camel(a: std.mem.Allocator, k: []const u8) ![]const u8 {
    var out: std.ArrayList(u8) = .empty;
    var i: usize = 0;
    while (i < k.len) : (i += 1) {
        const next: u8 = if (i + 1 < k.len) k[i + 1] else 0;
        if (k[i] == '-' and (std.ascii.isLower(next) or std.ascii.isDigit(next))) {
            try out.append(a, std.ascii.toUpper(next));
            i += 1;
        } else try out.append(a, k[i]);
    }
    return out.items;
}

pub const Value = union(enum) { text: []const u8, number: f64, flag: bool };

/// Booleans by word, numbers by shape, the rest as typed: what a console setter would get.
pub fn coerce(a: std.mem.Allocator, v: []const u8) !Value {
    const s = trimmed(v);
    const low = try lower(a, s);
    if (keys.oneOf(low, &keys.true_words)) return .{ .flag = true };
    if (keys.oneOf(low, &keys.false_words)) return .{ .flag = false };
    if (numberShape(s)) return .{ .number = std.fmt.parseFloat(f64, s) catch return .{ .text = v } };
    return .{ .text = v };
}
/// `^-?\d+(\.\d+)?$`
fn numberShape(s: []const u8) bool {
    var i: usize = if (s.len > 0 and s[0] == '-') 1 else 0;
    var digits: usize = 0;
    while (i < s.len and std.ascii.isDigit(s[i])) : (i += 1) digits += 1;
    if (digits == 0) return false;
    if (i == s.len) return true;
    if (s[i] != '.') return false;
    i += 1;
    var frac: usize = 0;
    while (i < s.len and std.ascii.isDigit(s[i])) : (i += 1) frac += 1;
    return frac > 0 and i == s.len;
}

/// application/x-www-form-urlencoded: `+` is a space, then percent escapes decode.
pub fn formDecode(a: std.mem.Allocator, raw: []const u8) ![]const u8 {
    const plus = try a.dupe(u8, raw);
    for (plus) |*c| if (c.* == '+') {
        c.* = ' ';
    };
    const buf = try a.alloc(u8, plus.len);
    return std.Uri.percentDecodeBackwards(buf, plus);
}
pub fn pairsOf(a: std.mem.Allocator, raw: []const u8, out: *std.ArrayList(Pair)) !void {
    var it = std.mem.splitScalar(u8, raw, '&');
    while (it.next()) |part| {
        if (part.len == 0) continue;
        const at = std.mem.indexOfScalar(u8, part, '=');
        const k = if (at) |i| part[0..i] else part;
        const v = if (at) |i| part[i + 1 ..] else "";
        try out.append(a, .{ .key = try kebab(a, try formDecode(a, k)), .value = try formDecode(a, v) });
    }
}

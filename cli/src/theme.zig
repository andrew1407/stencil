//! Brand-accent presets for the console UI, parsed from the canonical shared JSON
//! (browser/js/config/accents.json, embedded at build time) — the same rows the browser,
//! desktop and extension read; violet is the default. The accent colours the logo's panel
//! outline, the prompt and the echoed `/commands`. `logo.zig` consumes the chosen RGB;
//! `console.zig` exposes `/theme`.
const std = @import("std");
const logo = @import("logo.zig");

const accents_json = @embedFile("accents.json");

pub const Accent = struct {
    key: []const u8,
    label: []const u8,
    hex: []const u8,
    rgb: [3]u8,
};

pub const default_key = "violet";

/// Neutral grey for a project name with no custom colour — one fixed mid-grey, readable on
/// both light and dark terminals. Mirrors the browser's --project-name-fg and the desktop
/// default, so a default project reads as "unset" rather than wearing the brand accent.
pub const name_default_hex = "#80868f";

// Parsed lazily on first use: std.json needs an allocator, which comptime can't provide.
// The strings slice into the embedded JSON (static lifetime). A worker thread can reach
// this now that scrape fans its fetches out, so the one-shot parse is published through
// `parse_state`: the first caller fills the scratch, any other waits for it.
const unparsed = 0;
const parsing = 1;
const ready = 2;
var parse_state: std.atomic.Value(u8) = .init(unparsed);
var accent_count: usize = 0;
var accent_storage: [32]Accent = undefined;
var json_scratch: [4096]u8 = undefined;

/// The accent presets, parsed once from the embedded canonical accents.json.
pub fn accents() []const Accent {
    if (parse_state.load(.acquire) != ready) parseOnce();
    return accent_storage[0..accent_count];
}

fn parseOnce() void {
    if (parse_state.cmpxchgStrong(unparsed, parsing, .acquire, .acquire) == null) {
        parseAccents();
        parse_state.store(ready, .release);
        return;
    }
    while (parse_state.load(.acquire) != ready) std.atomic.spinLoopHint();
}

fn parseAccents() void {
    const Row = struct { key: []const u8, label: []const u8, hex: []const u8 };
    var fba = std.heap.FixedBufferAllocator.init(&json_scratch);
    const rows = std.json.parseFromSliceLeaky([]Row, fba.allocator(), accents_json, .{}) catch
        @panic("embedded accents.json is malformed");
    if (rows.len == 0 or rows.len > accent_storage.len) @panic("embedded accents.json: bad row count");
    for (rows, 0..) |row, i| {
        accent_storage[i] = .{ .key = row.key, .label = row.label, .hex = row.hex, .rgb = rgbFromHex(row.hex) };
    }
    accent_count = rows.len;
}

fn rgbFromHex(hex: []const u8) [3]u8 {
    if (hex.len != 7 or hex[0] != '#') @panic("embedded accents.json: bad hex");
    return .{ hexByte(hex[1..3]), hexByte(hex[3..5]), hexByte(hex[5..7]) };
}

fn hexByte(s: []const u8) u8 {
    return std.fmt.parseInt(u8, s, 16) catch @panic("embedded accents.json: bad hex");
}

/// Look up an accent by key (case-insensitive, accepts a leading '#' off the hex too).
pub fn find(key: []const u8) ?Accent {
    for (accents()) |a| {
        if (std.ascii.eqlIgnoreCase(a.key, key)) return a;
    }
    return null;
}

/// The RGB for a key, falling back to the default (violet) for an unknown key.
pub fn rgbOf(key: []const u8) [3]u8 {
    return (find(key) orelse accents()[0]).rgb;
}

/// Build a 24-bit truecolor SGR escape ("\x1b[38;2;r;g;bm") for a normalized "#rrggbb" hex
/// into `buf`, or null when the hex is malformed. Pure; the caller gates on colour being on.
/// `buf` needs room for the longest sequence ("\x1b[38;2;255;255;255m" = 19 bytes).
pub fn sgrForHex(hex: []const u8, buf: []u8) ?[]const u8 {
    if (hex.len != 7 or hex[0] != '#') return null;
    const r = std.fmt.parseInt(u8, hex[1..3], 16) catch return null;
    const g = std.fmt.parseInt(u8, hex[3..5], 16) catch return null;
    const b = std.fmt.parseInt(u8, hex[5..7], 16) catch return null;
    return std.fmt.bufPrint(buf, "\x1b[38;2;{d};{d};{d}m", .{ r, g, b }) catch null;
}

/// SGR escape painting a project name in its custom `color` ("#rrggbb") when set and parseable,
/// else the neutral default grey. "" when colour output is off (the name prints plain). The escape
/// is written into the caller's `buf`.
pub fn nameSeq(color: []const u8, buf: []u8) []const u8 {
    if (!logo.colorEnabled()) return "";
    if (color.len != 0) {
        if (sgrForHex(color, buf)) |s| return s;
    }
    return sgrForHex(name_default_hex, buf) orelse "";
}

pub fn hsvToRgb(h: f64, s: f64, v: f64) [3]u8 {
    const c = v * s;
    const hp = h / 60.0;
    const x = c * (1.0 - @abs(@mod(hp, 2.0) - 1.0));
    var r: f64 = 0;
    var g: f64 = 0;
    var b: f64 = 0;
    if (hp < 1) {
        r = c;
        g = x;
    } else if (hp < 2) {
        r = x;
        g = c;
    } else if (hp < 3) {
        g = c;
        b = x;
    } else if (hp < 4) {
        g = x;
        b = c;
    } else if (hp < 5) {
        r = x;
        b = c;
    } else {
        r = c;
        b = x;
    }
    const m = v - c;
    return .{
        @intFromFloat(@round((r + m) * 255.0)),
        @intFromFloat(@round((g + m) * 255.0)),
        @intFromFloat(@round((b + m) * 255.0)),
    };
}

const testing = std.testing;

test "theme: lookup, default fallback, case-insensitive" {
    try testing.expectEqualStrings("violet", default_key);
    try testing.expect(find("VIOLET").?.rgb[0] == 124);
    try testing.expect(find("brown") != null);
    try testing.expect(find("chartreuse") == null);
    try testing.expectEqual([3]u8{ 124, 58, 237 }, rgbOf("nope")); // unknown -> violet
    try testing.expectEqual([3]u8{ 100, 116, 139 }, rgbOf("grey"));
}

test "theme: default project-name colour is a parseable neutral grey, not an accent" {
    var buf: [20]u8 = undefined;
    try testing.expect(sgrForHex(name_default_hex, &buf) != null); // valid hex → it renders
    for (accents()) |a| {
        try testing.expect(!std.ascii.eqlIgnoreCase(a.hex, name_default_hex)); // never the accent
    }
}

test "theme: embedded accents.json parses to the canonical 16 presets" {
    const all = accents();
    try testing.expectEqual(@as(usize, 16), all.len);
    try testing.expectEqualStrings("violet", all[0].key); // default stays first
    const gray = all[14];
    try testing.expectEqualStrings("grey", gray.key);
    try testing.expectEqualStrings("Gray", gray.label); // canonical spelling, not "Grey"
    try testing.expectEqualStrings("#64748b", gray.hex);
    try testing.expectEqual([3]u8{ 100, 116, 139 }, gray.rgb); // rgb derived from hex
}

test "theme: sgrForHex builds a truecolor escape, rejects malformed hex" {
    var buf: [20]u8 = undefined;
    try testing.expectEqualStrings("\x1b[38;2;124;58;237m", sgrForHex("#7c3aed", &buf).?);
    try testing.expectEqualStrings("\x1b[38;2;255;255;255m", sgrForHex("#ffffff", &buf).?);
    try testing.expectEqualStrings("\x1b[38;2;0;0;0m", sgrForHex("#000000", &buf).?);
    try testing.expect(sgrForHex("", &buf) == null); // empty = no custom colour
    try testing.expect(sgrForHex("7c3aed", &buf) == null); // missing '#'
    try testing.expect(sgrForHex("#zzzzzz", &buf) == null); // non-hex digits
    try testing.expect(sgrForHex("#fff", &buf) == null); // wrong length
}

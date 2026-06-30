//! Brand-accent presets for the console UI. Keys, labels and hexes mirror the browser
//! (browser/js/core/accents.js), desktop (desktop/src/support/theme.cpp) and extension —
//! violet is the default. The accent colours the logo's panel outline, the prompt and the
//! echoed `/commands`. `logo.zig` consumes the chosen RGB; `console.zig` exposes `/theme`.
const std = @import("std");
const logo = @import("logo.zig");

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

pub const accents = [_]Accent{
    .{ .key = "violet", .label = "Violet", .hex = "#7c3aed", .rgb = .{ 124, 58, 237 } },
    .{ .key = "pink", .label = "Pink", .hex = "#ec4899", .rgb = .{ 236, 72, 153 } },
    .{ .key = "yellow", .label = "Yellow", .hex = "#eab308", .rgb = .{ 234, 179, 8 } },
    .{ .key = "orange", .label = "Orange", .hex = "#ea580c", .rgb = .{ 234, 88, 12 } },
    .{ .key = "crimson", .label = "Crimson", .hex = "#be123c", .rgb = .{ 190, 18, 60 } },
    .{ .key = "aqua", .label = "Aqua", .hex = "#0891b2", .rgb = .{ 8, 145, 178 } },
    .{ .key = "sky", .label = "Sky blue", .hex = "#0ea5e9", .rgb = .{ 14, 165, 233 } },
    .{ .key = "blue", .label = "Blue", .hex = "#2563eb", .rgb = .{ 37, 99, 235 } },
    .{ .key = "grass", .label = "Grass green", .hex = "#16a34a", .rgb = .{ 22, 163, 74 } },
    .{ .key = "green", .label = "Green", .hex = "#047857", .rgb = .{ 4, 120, 87 } },
    .{ .key = "brown", .label = "Brown", .hex = "#a87c50", .rgb = .{ 168, 124, 80 } },
    .{ .key = "grey", .label = "Grey", .hex = "#64748b", .rgb = .{ 100, 116, 139 } },
};

/// Look up an accent by key (case-insensitive, accepts a leading '#' off the hex too).
pub fn find(key: []const u8) ?Accent {
    for (accents) |a| {
        if (std.ascii.eqlIgnoreCase(a.key, key)) return a;
    }
    return null;
}

/// The RGB for a key, falling back to the default (violet) for an unknown key.
pub fn rgbOf(key: []const u8) [3]u8 {
    return (find(key) orelse accents[0]).rgb;
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
    for (accents) |a| {
        try testing.expect(!std.ascii.eqlIgnoreCase(a.hex, name_default_hex)); // never the accent
    }
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

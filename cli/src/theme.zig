//! Brand-accent presets for the console UI. Keys, labels and hexes mirror the browser
//! (browser/js/core/accents.js), desktop (desktop/src/support/theme.cpp) and extension —
//! violet is the default. The accent colours the logo's panel outline, the prompt and the
//! echoed `/commands`. `logo.zig` consumes the chosen RGB; `console.zig` exposes `/theme`.
const std = @import("std");

pub const Accent = struct {
    key: []const u8,
    label: []const u8,
    hex: []const u8,
    rgb: [3]u8,
};

pub const default_key = "violet";

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

const testing = std.testing;

test "theme: lookup, default fallback, case-insensitive" {
    try testing.expectEqualStrings("violet", default_key);
    try testing.expect(find("VIOLET").?.rgb[0] == 124);
    try testing.expect(find("brown") != null);
    try testing.expect(find("chartreuse") == null);
    try testing.expectEqual([3]u8{ 124, 58, 237 }, rgbOf("nope")); // unknown -> violet
    try testing.expectEqual([3]u8{ 100, 116, 139 }, rgbOf("grey"));
}

//! Drift guard for the CLI's brand colours: src/brand.zig scans the embedded
//! browser/js/config/themeTokens.json at COMPILE time (logo.zig needs constants), so this
//! re-reads the same bytes at runtime with std.json and pins every triple. It also holds the
//! cross-asset claims themeTokens' own brandNotes make — brand.accent is accents.json's
//! violet preset and the `--accent` light token — so the three canons cannot drift apart.
const std = @import("std");
const brand = @import("../src/app/brand.zig");
const theme = @import("../src/app/theme.zig");
const testing = std.testing;

const theme_tokens_json = @embedFile("themeTokens.json");

fn rgbOfHex(hex: []const u8) ![3]u8 {
    try testing.expect(hex.len == 7 and hex[0] == '#');
    return .{
        try std.fmt.parseInt(u8, hex[1..3], 16),
        try std.fmt.parseInt(u8, hex[3..5], 16),
        try std.fmt.parseInt(u8, hex[5..7], 16),
    };
}

test "brand.zig's comptime scan matches a real parse of themeTokens.json" {
    const a = testing.allocator;
    const parsed = try std.json.parseFromSlice(std.json.Value, a, theme_tokens_json, .{});
    defer parsed.deinit();
    const b = parsed.value.object.get("brand").?.object;

    const pairs = .{
        .{ "accent", brand.accent },
        .{ "annotation", brand.annotation },
        .{ "panel", brand.panel },
        .{ "panelInner", brand.panel_inner },
        .{ "panelGrid", brand.panel_grid },
        .{ "errorRed", brand.error_red },
    };
    inline for (pairs) |p| {
        const hex = b.get(p[0]) orelse {
            std.debug.print("themeTokens.json brand has no '{s}'\n", .{p[0]});
            return error.MissingBrandColour;
        };
        try testing.expectEqual(try rgbOfHex(hex.string), p[1]);
    }
    try testing.expectEqual(@as(usize, 6), b.count()); // a 7th would be one nobody reads
}

test "brand accent is accents.json's violet default and the --accent light token" {
    const a = testing.allocator;
    const parsed = try std.json.parseFromSlice(std.json.Value, a, theme_tokens_json, .{});
    defer parsed.deinit();
    const light = parsed.value.object.get("tokens").?.object.get("--accent").?.object.get("light").?;
    try testing.expectEqual(try rgbOfHex(light.string), brand.accent);
    try testing.expectEqual(theme.rgbOf(theme.default_key), brand.accent);
}

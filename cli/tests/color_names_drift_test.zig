//! Cross-language drift guard: the canonical CSS colour-name table
//! (browser/js/config/colorNames.json, embedded at build time) must resolve
//! identically through the linked C++ core's parser (core/color/colorNames.cpp,
//! reached via core.parseColor / stencil_cli_parseColor).
//!
//! Limitation: the core keeps its table in an anonymous namespace and exports no
//! enumeration through cliApi.h, so the check is one-directional — every JSON name
//! must parse to its JSON hex. Extra names known only to the core would go unseen;
//! the count pin (148) at least catches JSON-side additions/removals.
const std = @import("std");
const core = @import("../src/core.zig");

const color_names_json = @embedFile("colorNames.json");

fn hexByte(s: []const u8) !u8 {
    return std.fmt.parseInt(u8, s, 16);
}

test "colorNames.json: 148 entries, each resolves identically through the core" {
    const a = std.testing.allocator;
    const parsed = try std.json.parseFromSlice(std.json.Value, a, color_names_json, .{});
    defer parsed.deinit();
    const map = parsed.value.object;
    try std.testing.expectEqual(@as(usize, 148), map.count());

    var it = map.iterator();
    while (it.next()) |entry| {
        const name = entry.key_ptr.*;
        const hex = entry.value_ptr.*.string;
        try std.testing.expect(hex.len == 7 and hex[0] == '#'); // canonical "#rrggbb"
        const want = core.Rgba{
            .r = try hexByte(hex[1..3]),
            .g = try hexByte(hex[3..5]),
            .b = try hexByte(hex[5..7]),
            .a = 255,
        };
        const got = core.parseColor(a, name) orelse {
            std.debug.print("core does not recognise CSS name \"{s}\"\n", .{name});
            return error.NameMissingInCore;
        };
        std.testing.expectEqual(want, got) catch |err| {
            std.debug.print("core disagrees on \"{s}\": json {s}\n", .{ name, hex });
            return err;
        };
    }
}

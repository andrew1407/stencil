//! Cross-language drift guard: the canonical CSS colour-name table
//! (browser/js/config/colorNames.json, embedded at build time) and the linked C++
//! core's own table (core/color/colorNames.cpp) must hold the SAME names with the
//! same hexes. The core exports its table through cliApi.h (colorNameCount /
//! colorNameAt), so the check runs in both directions: a name known only to the
//! core, or only to the JSON, fails here.
const std = @import("std");
const core = @import("../src/core.zig");

const color_names_json = @embedFile("colorNames.json");

fn hexByte(s: []const u8) !u8 {
    return std.fmt.parseInt(u8, s, 16);
}

test "colorNames.json and the core's table agree, name for name, both ways" {
    const a = std.testing.allocator;
    const parsed = try std.json.parseFromSlice(std.json.Value, a, color_names_json, .{});
    defer parsed.deinit();
    const map = parsed.value.object;
    try std.testing.expectEqual(@as(usize, 148), map.count());
    try std.testing.expectEqual(map.count(), core.colorNameCount());

    // JSON -> core: every canon name resolves through the core to its canon hex.
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

    // core -> JSON: every name the core carries is in the canon, with the same hex.
    var i: usize = 0;
    while (i < core.colorNameCount()) : (i += 1) {
        const entry = core.colorNameAt(i).?;
        const canon = map.get(entry.name) orelse {
            std.debug.print("core knows CSS name \"{s}\", the JSON canon does not\n", .{entry.name});
            return error.NameMissingInJson;
        };
        var buf: [8]u8 = undefined;
        const hex = try std.fmt.bufPrint(&buf, "#{x:0>6}", .{entry.rgb});
        std.testing.expectEqualStrings(canon.string, hex) catch |err| {
            std.debug.print("hex drift on \"{s}\"\n", .{entry.name});
            return err;
        };
    }
}

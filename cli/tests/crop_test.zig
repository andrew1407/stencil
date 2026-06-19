// Decode the fixture, resolve a pixel crop spec, and crop the buffer.
const std = @import("std");
const core = @import("../src/core.zig");
const image = @import("../src/image.zig");
const testing = std.testing;
const sample = @embedFile("fixtures/sample.png");

test "resolveCrop + cropImageRGBA on the fixture" {
    const a = testing.allocator;
    var img = try image.decode(a, sample);
    defer img.deinit(a);
    const rect = core.resolveCrop(a, "x1=0px x2=8px y1=0px y2=6px", @floatFromInt(img.width), @floatFromInt(img.height), 16.0 / 21.0, 12.0 / 29.7, 21, 29.7, false).?;
    try testing.expectEqual(@as(i32, 8), rect.w);
    try testing.expectEqual(@as(i32, 6), rect.h);
    const dst = try a.alloc(u8, 8 * 6 * 4);
    defer a.free(dst);
    core.cropImageRGBA(img.pixels, @intCast(img.width), @intCast(img.height), rect, dst);
    try testing.expectEqual(@as(u8, 0x33), dst[0]); // solid colour preserved
    try testing.expectEqual(@as(u8, 0xcc), dst[2]);
}

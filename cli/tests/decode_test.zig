// Decode the committed PNG fixture (16x12 solid #3366cc) — exercises the stb decoder.
const std = @import("std");
const image = @import("../src/image.zig");
const testing = std.testing;
const sample = @embedFile("fixtures/sample.png");

test "decode the PNG fixture" {
    const a = testing.allocator;
    var img = try image.decode(a, sample);
    defer img.deinit(a);
    try testing.expectEqual(@as(usize, 16), img.width);
    try testing.expectEqual(@as(usize, 12), img.height);
    try testing.expectEqual(@as(u8, 0x33), img.pixels[0]);
    try testing.expectEqual(@as(u8, 0x66), img.pixels[1]);
    try testing.expectEqual(@as(u8, 0xcc), img.pixels[2]);
    try testing.expectEqual(@as(u8, 0xff), img.pixels[3]);
}

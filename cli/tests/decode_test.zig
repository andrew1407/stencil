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

test "decode refuses a header claiming absurd dimensions before allocating" {
    const a = testing.allocator;
    // A PNG IHDR claiming 30000x30000 (2.7 GiB of RGBA8): over STBI_MAX_DIMENSIONS, so the
    // decoder itself refuses it — no pixel buffer is ever asked for.
    const png = [_]u8{ 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a } ++ [_]u8{ 0, 0, 0, 13, 'I', 'H', 'D', 'R' } ++
        [_]u8{ 0, 0, 0x75, 0x30, 0, 0, 0x75, 0x30, 8, 2, 0, 0, 0 } ++ [_]u8{ 0, 0, 0, 0 };
    try testing.expectError(error.ImageDecodeFailed, image.decode(a, &png));

    // A TGA header claiming 40000x40000: each side is under the per-side cap, so it is the
    // pixel-AREA cap that refuses it (w*h*4 would overflow the c_int handed back to stb).
    const tga = [_]u8{ 0, 0, 2 } ++ [_]u8{0} ** 9 ++ [_]u8{ 0x40, 0x9c, 0x40, 0x9c, 24, 0 };
    try testing.expectError(error.ImageTooLarge, image.decode(a, &tga));
}

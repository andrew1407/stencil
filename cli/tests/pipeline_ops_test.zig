// The raster ops the pipeline composes, run on ONE decode of the committed PNG fixture
// (16x12 solid #3366cc): crop, quarter-rotate, and the per-format encode/decode seam.
// image.zig's own inline tests cover the codec in isolation; these pin the core ops on
// real decoded pixels, and the decode guards that run before any buffer is allocated.
const std = @import("std");
const core = @import("../src/core.zig");
const image = @import("../src/media/image.zig");
const testing = std.testing;
const sample = @embedFile("fixtures/sample.png");

/// The decoded fixture, checked against its pinned dimensions and first pixel before any
/// op runs — so a codec change surfaces here instead of as a mangled crop/rotate result.
fn openSample(a: std.mem.Allocator) !image.Rgba8 {
    var img = try image.decode(a, sample);
    errdefer img.deinit(a);
    try testing.expectEqual(@as(usize, 16), img.width);
    try testing.expectEqual(@as(usize, 12), img.height);
    try testing.expectEqualSlices(u8, &.{ 0x33, 0x66, 0xcc, 0xff }, img.pixels[0..4]);
    return img;
}

test "ops: resolveCrop + cropImageRGBA on the fixture" {
    const a = testing.allocator;
    var img = try openSample(a);
    defer img.deinit(a);

    const rect = core.resolveCrop("x1=0px x2=8px y1=0px y2=6px", @floatFromInt(img.width), @floatFromInt(img.height), 16.0 / 21.0, 12.0 / 29.7, 21, 29.7, false).?;
    try testing.expectEqual(@as(i32, 8), rect.w);
    try testing.expectEqual(@as(i32, 6), rect.h);
    const dst = try a.alloc(u8, 8 * 6 * 4);
    defer a.free(dst);
    core.cropImageRGBA(img.pixels, @intCast(img.width), @intCast(img.height), rect, dst);
    try testing.expectEqual(@as(u8, 0x33), dst[0]); // solid colour preserved
    try testing.expectEqual(@as(u8, 0xcc), dst[2]);
}

test "ops: a quarter turn swaps dimensions, and survives a png round-trip" {
    const a = testing.allocator;
    var img = try openSample(a);
    defer img.deinit(a);

    const dims = core.rotatedDims(@intCast(img.width), @intCast(img.height), 1);
    try testing.expectEqual(@as(i32, 12), dims.w);
    try testing.expectEqual(@as(i32, 16), dims.h);

    const n = @as(usize, @intCast(dims.w)) * @as(usize, @intCast(dims.h)) * 4;
    const dst = try a.alloc(u8, n);
    defer a.free(dst);
    core.rotateImageRGBA(img.pixels, @intCast(img.width), @intCast(img.height), 1, dst);

    const out = image.Rgba8{ .width = @intCast(dims.w), .height = @intCast(dims.h), .pixels = dst };
    const enc = try image.encode(a, out, .png);
    defer a.free(enc);
    var back = try image.decode(a, enc);
    defer back.deinit(a);
    try testing.expectEqual(@as(usize, 12), back.width);
    try testing.expectEqual(@as(usize, 16), back.height);
}

test "ops: every output format round-trips dimensions" {
    const a = testing.allocator;
    var px = [_]u8{
        10,  20,  30,  255, 40,  50,  60,  255, 70,  80,  90,  255,
        130, 140, 150, 255, 160, 170, 180, 255, 190, 200, 210, 255,
    };
    const img = image.Rgba8{ .width = 3, .height = 2, .pixels = &px };
    inline for (.{ image.Format.png, image.Format.jpeg, image.Format.bmp, image.Format.tga }) |fmt| {
        const enc = try image.encode(a, img, fmt);
        defer a.free(enc);
        try testing.expect(enc.len > 0);
        var dec = try image.decode(a, enc);
        defer dec.deinit(a);
        try testing.expectEqual(@as(usize, 3), dec.width);
        try testing.expectEqual(@as(usize, 2), dec.height);
    }
}

test "ops: decode refuses a header claiming absurd dimensions before allocating" {
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

// Rotate the fixture a quarter turn and confirm the dimensions swap (via re-encode).
const std = @import("std");
const core = @import("../src/core.zig");
const image = @import("../src/image.zig");
const testing = std.testing;
const sample = @embedFile("fixtures/sample.png");

test "rotate the fixture a quarter turn swaps dimensions" {
    const a = testing.allocator;
    var img = try image.decode(a, sample);
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

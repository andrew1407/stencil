//! `--thumbnail`: the result shrunk so its longer side fits, by core's area-average downscale
//! (core/raster/downscale.hpp). It holds that slice of the C ABI: nothing else calls it, and
//! core.zig stays the whole-image/scalar bridge.
const std = @import("std");
const core = @import("../core.zig");
const image = @import("image.zig");

const c = @cImport({
    @cInclude("core/cliApi.h");
});

/// The size whose longer side is at most `max_side`, aspect kept; an image that fits keeps its own.
pub fn dims(w: usize, h: usize, max_side: u32) core.Size {
    var ow: c_int = 0;
    var oh: c_int = 0;
    const side: c_int = @intCast(@min(max_side, std.math.maxInt(c_int)));
    c.stencil_cli_thumbnailDims(@intCast(w), @intCast(h), side, &ow, &oh);
    return .{ .w = @intCast(ow), .h = @intCast(oh) };
}

/// Replace `img` with its thumbnail. Never upscales: an image that already fits is untouched.
pub fn shrink(gpa: std.mem.Allocator, img: *image.Rgba8, max_side: u32) !void {
    const to = dims(img.width, img.height, max_side);
    const uw: usize = @intCast(to.w);
    const uh: usize = @intCast(to.h);
    if (uw == img.width and uh == img.height) return;
    const dst = try gpa.alloc(u8, uw * uh * 4);
    errdefer gpa.free(dst);
    if (c.stencil_cli_downscaleRGBA(img.pixels.ptr, @intCast(img.width), @intCast(img.height), dst.ptr, to.w, to.h) == 0)
        return error.BadThumbnail;
    img.deinit(gpa);
    img.* = .{ .width = uw, .height = uh, .pixels = dst };
}

const testing = std.testing;

test "dims: the longer side fits, the aspect is kept, nothing grows" {
    try testing.expectEqual(core.Size{ .w = 512, .h = 384 }, dims(4000, 3000, 512));
    try testing.expectEqual(core.Size{ .w = 384, .h = 512 }, dims(3000, 4000, 512));
    try testing.expectEqual(core.Size{ .w = 16, .h = 12 }, dims(16, 12, 100));
    try testing.expectEqual(core.Size{ .w = 16, .h = 12 }, dims(16, 12, std.math.maxInt(u32)));
}

test "shrink: a solid image stays its colour at the new size; a fitting one is untouched" {
    const a = testing.allocator;
    var img = image.Rgba8{ .width = 6, .height = 3, .pixels = try a.alloc(u8, 6 * 3 * 4) };
    defer img.deinit(a);
    for (0..18) |i| @memcpy(img.pixels[i * 4 ..][0..4], &[_]u8{ 10, 20, 30, 255 });
    const before = img.pixels.ptr;
    try shrink(a, &img, 6);
    try testing.expectEqual(before, img.pixels.ptr);
    try shrink(a, &img, 4);
    try testing.expectEqual(@as(usize, 4), img.width);
    try testing.expectEqual(@as(usize, 2), img.height);
    for (0..8) |i| try testing.expectEqualSlices(u8, &.{ 10, 20, 30, 255 }, img.pixels[i * 4 ..][0..4]);
}

//! Rect geometry the console's derived view needs — clamping a crop into an image and carrying
//! one through clockwise quarter-turns, both through the core — plus the free-on-error helper.
const std = @import("std");
const image = @import("../../media/image.zig");
const core = @import("../../core.zig");

pub fn freeImg(gpa: std.mem.Allocator, img: image.Rgba8, e: anyerror) anyerror {
    var m = img;
    m.deinit(gpa);
    return e;
}

/// Clamp a rect to lie within a `w`×`h` image (width/height ≥ 1): the browser's crop commit.
pub fn clampRect(r: core.Rect, w: i32, h: i32) core.Rect {
    return core.snapCropRect(r, w, h);
}

/// Carry a crop shown at `rotation` through `n` clockwise quarter-turns of the unturned
/// `orig_w`×`orig_h` original, one core step (the browser's rotate) at a time.
pub fn rotateCropQuarters(crop: core.Rect, rotation: i32, orig_w: i32, orig_h: i32, n: i32) core.EditTurn {
    var t = core.EditTurn{ .crop = crop, .quarters = core.normalizeQuarters(rotation) };
    var q = core.normalizeQuarters(n);
    while (q > 0) : (q -= 1) t = core.rotateEditQuarter(t.crop, t.quarters, orig_w, orig_h, true);
    return t;
}

const testing = std.testing;

test "rotateCropQuarters maps a crop through clockwise quarter-turns" {
    // A 10x4 rect at (1,2) in a 100x50 original, rotated once clockwise → 50x100 image.
    const r1 = rotateCropQuarters(.{ .x = 1, .y = 2, .w = 10, .h = 4 }, 0, 100, 50, 1).crop;
    // new x = ch - y - rh = 50 - 2 - 4 = 44; new y = x = 1; w=rh=4; h=rw=10.
    try testing.expectEqual(@as(i32, 44), r1.x);
    try testing.expectEqual(@as(i32, 1), r1.y);
    try testing.expectEqual(@as(i32, 4), r1.w);
    try testing.expectEqual(@as(i32, 10), r1.h);
    // Four turns returns to the original.
    const r4 = rotateCropQuarters(.{ .x = 1, .y = 2, .w = 10, .h = 4 }, 0, 100, 50, 4).crop;
    try testing.expectEqual(@as(i32, 1), r4.x);
    try testing.expectEqual(@as(i32, 2), r4.y);
    try testing.expectEqual(@as(i32, 10), r4.w);
    try testing.expectEqual(@as(i32, 4), r4.h);
    // Three clockwise turns are one anticlockwise one, and the count wraps with them.
    const back = rotateCropQuarters(.{ .x = 1, .y = 2, .w = 10, .h = 4 }, 1, 100, 50, -1);
    try testing.expectEqual(@as(i32, 0), back.quarters);
}

test "clampRect keeps a window whole and moves it inside, as the browser commits a crop" {
    const r = clampRect(.{ .x = 95, .y = -3, .w = 20, .h = 400 }, 100, 50);
    try testing.expectEqual(core.Rect{ .x = 80, .y = 0, .w = 20, .h = 50 }, r);
    // An image smaller than a pixel no longer trips std.math.clamp's lower > upper assert.
    try testing.expectEqual(core.Rect{ .x = 0, .y = 0, .w = 1, .h = 1 }, clampRect(.{ .x = 3, .y = 3, .w = 5, .h = 5 }, 0, 0));
}

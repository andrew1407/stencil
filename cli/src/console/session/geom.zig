//! Rect geometry the console's derived view needs: clamping a crop into an image and
//! mapping one through clockwise quarter-turns, plus the free-on-error image helper.
const std = @import("std");
const image = @import("../../image.zig");
const core = @import("../../core.zig");

pub fn freeImg(gpa: std.mem.Allocator, img: image.Rgba8, e: anyerror) anyerror {
    var m = img;
    m.deinit(gpa);
    return e;
}

/// Clamp a rect to lie within a `w`×`h` image (width/height ≥ 1).
pub fn clampRect(r: core.Rect, w: i32, h: i32) core.Rect {
    var out = r;
    out.w = std.math.clamp(r.w, 1, w);
    out.h = std.math.clamp(r.h, 1, h);
    out.x = std.math.clamp(r.x, 0, w - out.w);
    out.y = std.math.clamp(r.y, 0, h - out.h);
    return out;
}

/// Map a rect through `n` clockwise quarter-turns of its `w`×`h` containing image, returning
/// the rect in the rotated image's pixel space. Pure (axis-aligned 90° steps). Unit-tested.
pub fn rotateRectQuarters(rect: core.Rect, w: i32, h: i32, n: i32) core.Rect {
    var r = rect;
    var cw = w;
    var ch = h;
    var q = core.normalizeQuarters(n);
    while (q > 0) : (q -= 1) {
        // One clockwise step: new dims (ch, cw); (x,y) → (ch - y - rh, x). Compute into a
        // temp first — assigning a struct literal that reads `r` would alias the in-place write.
        const nr = core.Rect{ .x = ch - r.y - r.h, .y = r.x, .w = r.h, .h = r.w };
        r = nr;
        const t = cw;
        cw = ch;
        ch = t;
    }
    return r;
}

/// Rasterize the lines in a JSON array string onto `img` (best-effort; bad JSON draws nothing).
const testing = std.testing;

test "rotateRectQuarters maps a rect through clockwise quarter-turns" {
    // A 10x4 rect at (1,2) in a 100x50 image, rotated once clockwise → 50x100 image.
    const r1 = rotateRectQuarters(.{ .x = 1, .y = 2, .w = 10, .h = 4 }, 100, 50, 1);
    // new x = ch - y - rh = 50 - 2 - 4 = 44; new y = x = 1; w=rh=4; h=rw=10.
    try testing.expectEqual(@as(i32, 44), r1.x);
    try testing.expectEqual(@as(i32, 1), r1.y);
    try testing.expectEqual(@as(i32, 4), r1.w);
    try testing.expectEqual(@as(i32, 10), r1.h);
    // Four turns returns to the original.
    const r4 = rotateRectQuarters(.{ .x = 1, .y = 2, .w = 10, .h = 4 }, 100, 50, 4);
    try testing.expectEqual(@as(i32, 1), r4.x);
    try testing.expectEqual(@as(i32, 2), r4.y);
    try testing.expectEqual(@as(i32, 10), r4.w);
    try testing.expectEqual(@as(i32, 4), r4.h);
}

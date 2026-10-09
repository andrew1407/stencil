//! The crop and quarter-turn half of the core bridge (core/geometry/cropSnap.hpp,
//! core/parse/cropSpec.hpp, core/raster/imageOps.hpp): resolving a crop spec, committing a crop
//! window, what a crop does to the lines, one quarter-turn of an edit, and the pixel crop and
//! turn. core.zig re-exports them.
const std = @import("std");
const testing = std.testing;

const c = @cImport({
    @cInclude("core/cliApi.h");
});

pub const Rect = struct { x: i32, y: i32, w: i32, h: i32 };
pub const Size = struct { w: i32, h: i32 };

/// A spec core cannot read (a bad token, an edge outside the image), or one that keeps nothing.
pub const CropError = error{ BadCropSpec, EmptyCrop };

/// Resolve a crop spec string to a clamped integer pixel rect.
pub fn resolveCrop(
    spec: [:0]const u8,
    image_w: f64,
    image_h: f64,
    px_per_cm_x: f64,
    px_per_cm_y: f64,
    page_w_cm: f64,
    page_h_cm: f64,
    album: bool,
) CropError!Rect {
    var x: c_int = 0;
    var y: c_int = 0;
    var w: c_int = 0;
    var h: c_int = 0;
    const ok = c.stencil_cli_resolveCrop(spec.ptr, image_w, image_h, px_per_cm_x, px_per_cm_y, page_w_cm, page_h_cm, @intFromBool(album), &x, &y, &w, &h);
    if (ok == 0) return error.BadCropSpec;
    if (ok < 0) return error.EmptyCrop;
    return .{ .x = @intCast(x), .y = @intCast(y), .w = @intCast(w), .h = @intCast(h) };
}

pub fn cropImageRGBA(src: []const u8, src_w: i32, src_h: i32, rect: Rect, dst: []u8) void {
    c.stencil_cli_cropImageRGBA(src.ptr, src_w, src_h, rect.x, rect.y, rect.w, rect.h, dst.ptr);
}

pub fn normalizeQuarters(q: i32) i32 {
    return c.stencil_cli_normalizeQuarters(q);
}

/// A crop window committed to integer pixels inside an `image_w`×`image_h` image: each side kept
/// in [1, the image's side], then the origin moved inside (core::snapCropRect).
pub fn snapCropRect(r: Rect, image_w: i32, image_h: i32) Rect {
    var out: [4]f64 = undefined;
    c.stencil_cli_snapCropRect(@floatFromInt(r.x), @floatFromInt(r.y), @floatFromInt(r.w), @floatFromInt(r.h), @floatFromInt(image_w), @floatFromInt(image_h), &out);
    return rectOf(out[0..4]);
}

pub const CropChange = struct { orientation_changed: bool, scale: f64 };

/// What a crop window's move from `old` to `new` does to the lines, as every editor recalcs them:
/// an album/portrait flip clears them, else they scale by the width ratio (core::cropChange).
pub fn cropChange(old: Rect, new: Rect) CropChange {
    var out: [2]f64 = undefined;
    c.stencil_cli_cropChange(@floatFromInt(old.x), @floatFromInt(old.y), @floatFromInt(old.w), @floatFromInt(old.h), @floatFromInt(new.x), @floatFromInt(new.y), @floatFromInt(new.w), @floatFromInt(new.h), &out);
    return .{ .orientation_changed = out[0] != 0, .scale = out[1] };
}

pub const EditTurn = struct { crop: Rect, quarters: i32 };

/// One quarter-turn of an edit shown at `quarters` over the unturned `orig_w`×`orig_h` original:
/// the crop follows into the turned space, snapped, and the count wraps (core::rotateEditQuarter).
pub fn rotateEditQuarter(crop: Rect, quarters: i32, orig_w: i32, orig_h: i32, clockwise: bool) EditTurn {
    var out: [5]f64 = undefined;
    c.stencil_cli_rotateEditQuarter(@floatFromInt(crop.x), @floatFromInt(crop.y), @floatFromInt(crop.w), @floatFromInt(crop.h), quarters, @floatFromInt(orig_w), @floatFromInt(orig_h), @intFromBool(clockwise), &out);
    return .{ .crop = rectOf(out[0..4]), .quarters = @intFromFloat(out[4]) };
}

/// A left-right flip of an edit shown at `quarters` (the caller toggles its mirror flag): the crop
/// reflects across the turned width, snapped, and the count negates (core::mirrorEdit).
pub fn mirrorEdit(crop: Rect, quarters: i32, orig_w: i32, orig_h: i32) EditTurn {
    var out: [5]f64 = undefined;
    c.stencil_cli_mirrorEdit(@floatFromInt(crop.x), @floatFromInt(crop.y), @floatFromInt(crop.w), @floatFromInt(crop.h), quarters, @floatFromInt(orig_w), @floatFromInt(orig_h), &out);
    return .{ .crop = rectOf(out[0..4]), .quarters = @intFromFloat(out[4]) };
}

// Integer inputs snap to integral, finite values, so the casts cannot trap.
fn rectOf(v: []const f64) Rect {
    return .{ .x = @intFromFloat(v[0]), .y = @intFromFloat(v[1]), .w = @intFromFloat(v[2]), .h = @intFromFloat(v[3]) };
}

pub fn rotatedDims(w: i32, h: i32, quarters: i32) Size {
    var ow: c_int = 0;
    var oh: c_int = 0;
    c.stencil_cli_rotatedDims(w, h, quarters, &ow, &oh);
    return .{ .w = @intCast(ow), .h = @intCast(oh) };
}

pub fn rotateImageRGBA(src: []const u8, w: i32, h: i32, quarters: i32, dst: []u8) void {
    c.stencil_cli_rotateImageRGBA(src.ptr, w, h, quarters, dst.ptr);
}

/// Left-right; `dst` is w*h*4 bytes and never `src`.
pub fn mirrorImageRGBA(src: []const u8, w: i32, h: i32, dst: []u8) void {
    c.stencil_cli_mirrorImageRows(src.ptr, w, h, dst.ptr, 0, h);
}

test "resolveCrop + rotate helpers" {
    const rect = try resolveCrop("x1=0px x2=100px y1=0px y2=50px", 200, 200, 10, 10, 21, 29.7, false);
    try testing.expectEqual(@as(i32, 100), rect.w);
    try testing.expectEqual(@as(i32, 50), rect.h);
    try testing.expectError(error.BadCropSpec, resolveCrop("z=1", 200, 200, 10, 10, 21, 29.7, false));
    try testing.expectError(error.BadCropSpec, resolveCrop("y2=-200%", 200, 200, 10, 10, 21, 29.7, false));
    try testing.expectError(error.EmptyCrop, resolveCrop("x1=50px x2=50px", 200, 200, 10, 10, 21, 29.7, false));
    try testing.expectEqual(@as(i32, 3), normalizeQuarters(-1));
    const d = rotatedDims(4, 2, 1);
    try testing.expect(d.w == 2 and d.h == 4);
}

test "mirrorEdit reflects the crop and negates the turn; mirrorImageRGBA reverses each row" {
    const t = mirrorEdit(.{ .x = 10, .y = 20, .w = 80, .h = 40 }, 1, 200, 100);
    try testing.expectEqual(Rect{ .x = 10, .y = 20, .w = 80, .h = 40 }, t.crop);
    try testing.expectEqual(@as(i32, 3), t.quarters);
    try testing.expectEqual(Rect{ .x = 110, .y = 20, .w = 80, .h = 40 }, mirrorEdit(.{ .x = 10, .y = 20, .w = 80, .h = 40 }, 0, 200, 100).crop);
    const src = [_]u8{ 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3 };
    var dst: [12]u8 = undefined;
    mirrorImageRGBA(&src, 3, 1, &dst);
    try testing.expectEqualSlices(u8, &.{ 3, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1 }, &dst);
}

test "cropChange flips on album/portrait, else scales by the width ratio" {
    const album = Rect{ .x = 0, .y = 0, .w = 16, .h = 12 };
    try testing.expectEqual(CropChange{ .orientation_changed = false, .scale = 0.5 }, cropChange(album, .{ .x = 4, .y = 3, .w = 8, .h = 6 }));
    try testing.expectEqual(CropChange{ .orientation_changed = true, .scale = 1 }, cropChange(album, .{ .x = 0, .y = 0, .w = 8, .h = 12 }));
    // A square is portrait (width > height is album), so squaring an album window flips it.
    try testing.expect(cropChange(album, .{ .x = 0, .y = 0, .w = 12, .h = 12 }).orientation_changed);
    try testing.expectEqual(@as(f64, 1), cropChange(album, album).scale);
}

// The rule cropChange computed in Zig before it asked core: the oracle for the table below.
fn oldCropChange(old: Rect, new: Rect) CropChange {
    const flipped = (old.w > old.h) != (new.w > new.h);
    if (flipped or old.w <= 0) return .{ .orientation_changed = flipped, .scale = 1 };
    return .{ .orientation_changed = false, .scale = @as(f64, @floatFromInt(new.w)) / @as(f64, @floatFromInt(old.w)) };
}

test "cropChange through core answers exactly as the old Zig rule did" {
    const big = std.math.maxInt(i32);
    const rects = [_]Rect{
        .{ .x = 0, .y = 0, .w = 16, .h = 12 },
        .{ .x = 4, .y = 3, .w = 8, .h = 6 },
        .{ .x = 0, .y = 0, .w = 12, .h = 12 }, // square: portrait
        .{ .x = -3, .y = -3, .w = 0, .h = 5 }, // a zero width
        .{ .x = 0, .y = 0, .w = -4, .h = -9 }, // negative
        .{ .x = 0, .y = 0, .w = 0, .h = 0 },
        .{ .x = 9, .y = 9, .w = big, .h = 1 }, // oversize
        .{ .x = 0, .y = 0, .w = 3, .h = 7 },
    };
    for (rects) |old| for (rects) |new| {
        try testing.expectEqual(oldCropChange(old, new), cropChange(old, new));
        const turned = rotateEditQuarter(new, 0, 40, 30, true).crop; // a window after a quarter-turn
        try testing.expectEqual(oldCropChange(old, turned), cropChange(old, turned));
    };
}

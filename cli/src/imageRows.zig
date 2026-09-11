//! Row-parallel image transforms. The core exposes half-open [y0,y1) row slices of its
//! whole-image ops (core/cliApi.h, "Row ranges") precisely because it owns no threading
//! policy — this file owns the CLI's. It also holds the row slice of the C ABI: nothing
//! else calls it, and core.zig stays the whole-image/scalar bridge.
//! Bands write disjoint rows, so the output is byte-identical to the serial call.
const std = @import("std");
const core = @import("core.zig");

const c = @cImport({
    @cInclude("core/cliApi.h");
});

/// Upper bound on bands. Past a handful of threads these ops are memory-bandwidth bound,
/// and each band costs a thread spawn.
const max_bands = 8;

/// Below this the spawns cost more than the work they save; stay on one thread.
const min_parallel_pixels: usize = 1 << 18;

fn bandCount(rows: i32, pixels: usize) usize {
    if (pixels < min_parallel_pixels or rows < 2) return 1;
    const cpus = std.Thread.getCpuCount() catch return 1;
    return @min(@min(cpus, max_bands), @as(usize, @intCast(rows)));
}

/// Row `i` of `n` even bands over `rows` (edge(0) = 0, edge(n) = rows).
fn edge(rows: i32, n: usize, i: usize) i32 {
    return @intCast(@divTrunc(@as(i64, rows) * @as(i64, @intCast(i)), @as(i64, @intCast(n))));
}

/// Run `band(ctx, y0, y1)` over `n` bands of `rows` and return once every band is done.
/// The calling thread takes the last band, plus any band that would not spawn.
fn spread(rows: i32, n: usize, ctx: anytype, comptime band: fn (@TypeOf(ctx), i32, i32) void) void {
    if (n < 2) return band(ctx, 0, rows);
    var threads: [max_bands]std.Thread = undefined;
    var live: usize = 0;
    while (live < n - 1) : (live += 1) {
        threads[live] = std.Thread.spawn(.{}, band, .{ ctx, edge(rows, n, live), edge(rows, n, live + 1) }) catch break;
    }
    band(ctx, edge(rows, n, live), rows);
    for (threads[0..live]) |t| t.join();
}

const Crop = struct {
    src: []const u8,
    src_w: i32,
    src_h: i32,
    rect: core.Rect,
    dst: []u8,

    fn band(s: Crop, y0: i32, y1: i32) void {
        c.stencil_cli_cropImageRows(s.src.ptr, s.src_w, s.src_h, s.rect.x, s.rect.y,
            s.rect.w, s.rect.h, s.dst.ptr, y0, y1);
    }
};

/// core.cropImageRGBA, band-parallel over the destination rows.
pub fn crop(src: []const u8, src_w: i32, src_h: i32, rect: core.Rect, dst: []u8) void {
    const ctx = Crop{ .src = src, .src_w = src_w, .src_h = src_h, .rect = rect, .dst = dst };
    spread(rect.h, bandCount(rect.h, dst.len / 4), ctx, Crop.band);
}

const Rotate = struct {
    src: []const u8,
    w: i32,
    h: i32,
    quarters: i32,
    dst: []u8,

    fn band(s: Rotate, y0: i32, y1: i32) void {
        c.stencil_cli_rotateImageRows(s.src.ptr, s.w, s.h, s.quarters, s.dst.ptr, y0, y1);
    }
};

/// core.rotateImageRGBA, band-parallel over the rotated (destination) rows.
pub fn rotate(src: []const u8, w: i32, h: i32, quarters: i32, dst: []u8) void {
    const dims = core.rotatedDims(w, h, quarters);
    const ctx = Rotate{ .src = src, .w = w, .h = h, .quarters = quarters, .dst = dst };
    spread(dims.h, bandCount(dims.h, dst.len / 4), ctx, Rotate.band);
}

const Filter = struct {
    mode: [:0]const u8,
    data: []u8,
    w: i32,
    tint: core.Rgba,

    fn band(s: Filter, y0: i32, y1: i32) void {
        c.stencil_cli_applyFilterRows(s.mode.ptr, s.data.ptr, s.w, y0, y1, s.tint.r, s.tint.g, s.tint.b);
    }
};

/// core.applyFilter, band-parallel. The row call trusts y1, so the bands are cut from
/// the height the caller passes and never clamped away.
pub fn filter(mode: [:0]const u8, data: []u8, w: i32, h: i32, tint: core.Rgba) void {
    const ctx = Filter{ .mode = mode, .data = data, .w = w, .tint = tint };
    spread(h, bandCount(h, @intCast(@as(i64, w) * @as(i64, h))), ctx, Filter.band);
}

const Luma = struct {
    data: []const u8,
    w: i32,
    h: i32,
    luma: []u8,

    fn band(s: Luma, y0: i32, y1: i32) void {
        c.stencil_cli_buildLumaRows(s.data.ptr, s.w, s.h, y0, y1, s.luma.ptr);
    }
};

const Sobel = struct {
    luma: []const u8,
    data: []u8,
    w: i32,
    h: i32,

    fn band(s: Sobel, y0: i32, y1: i32) void {
        c.stencil_cli_sobelRows(s.luma.ptr, s.data.ptr, s.w, s.h, y0, y1);
    }
};

/// core.applyContour, band-parallel in the two phases the ABI requires: the Sobel pass
/// reads a row outside its band, so every luma row is built before any of it runs.
/// Falls back to the whole-image call when there is no scratch for the luma plane.
pub fn contour(gpa: std.mem.Allocator, data: []u8, w: i32, h: i32) void {
    const pixels: usize = @intCast(@as(i64, w) * @as(i64, h));
    const n = bandCount(h, pixels);
    if (n < 2) return core.applyContour(data, w, h);
    const luma = gpa.alloc(u8, pixels) catch return core.applyContour(data, w, h);
    defer gpa.free(luma);
    spread(h, n, Luma{ .data = data, .w = w, .h = h, .luma = luma }, Luma.band);
    spread(h, n, Sobel{ .luma = luma, .data = data, .w = w, .h = h }, Sobel.band);
}

const testing = std.testing;

// A gradient big enough to cross min_parallel_pixels, so the banded path really runs.
fn gradient(a: std.mem.Allocator, w: usize, h: usize) ![]u8 {
    const buf = try a.alloc(u8, w * h * 4);
    for (0..h) |y| for (0..w) |x| {
        const i = (y * w + x) * 4;
        buf[i + 0] = @truncate(x);
        buf[i + 1] = @truncate(y);
        buf[i + 2] = @truncate(x ^ y);
        buf[i + 3] = 255;
    };
    return buf;
}

test "banded crop/rotate/filter/contour match the whole-image calls byte for byte" {
    const a = testing.allocator;
    const w = 600;
    const h = 500;
    const src = try gradient(a, w, h);
    defer a.free(src);
    try testing.expect(w * h > min_parallel_pixels); // the bands really are used

    const rect = core.Rect{ .x = 7, .y = 11, .w = 400, .h = 300 };
    const banded_crop = try a.alloc(u8, 400 * 300 * 4);
    defer a.free(banded_crop);
    const serial_crop = try a.alloc(u8, 400 * 300 * 4);
    defer a.free(serial_crop);
    crop(src, w, h, rect, banded_crop);
    core.cropImageRGBA(src, w, h, rect, serial_crop);
    try testing.expectEqualSlices(u8, serial_crop, banded_crop);

    const banded_rot = try a.alloc(u8, w * h * 4);
    defer a.free(banded_rot);
    const serial_rot = try a.alloc(u8, w * h * 4);
    defer a.free(serial_rot);
    rotate(src, w, h, 1, banded_rot);
    core.rotateImageRGBA(src, w, h, 1, serial_rot);
    try testing.expectEqualSlices(u8, serial_rot, banded_rot);

    const banded_px = try a.dupe(u8, src);
    defer a.free(banded_px);
    const serial_px = try a.dupe(u8, src);
    defer a.free(serial_px);
    filter("bw", banded_px, w, h, .{ .r = 0, .g = 0, .b = 0, .a = 255 });
    core.applyFilter("bw", serial_px, w * h, .{ .r = 0, .g = 0, .b = 0, .a = 255 });
    try testing.expectEqualSlices(u8, serial_px, banded_px);

    @memcpy(banded_px, src);
    @memcpy(serial_px, src);
    contour(a, banded_px, w, h);
    core.applyContour(serial_px, w, h);
    try testing.expectEqualSlices(u8, serial_px, banded_px);
}

test "a small image stays on one band and still transforms" {
    const a = testing.allocator;
    const src = try gradient(a, 8, 4);
    defer a.free(src);
    try testing.expectEqual(@as(usize, 1), bandCount(4, 32));
    const dst = try a.alloc(u8, 8 * 4 * 4);
    defer a.free(dst);
    rotate(src, 8, 4, 2, dst);
    const serial = try a.alloc(u8, 8 * 4 * 4);
    defer a.free(serial);
    core.rotateImageRGBA(src, 8, 4, 2, serial);
    try testing.expectEqualSlices(u8, serial, dst);
}

test "bands tile the rows exactly, with no gap or overlap" {
    for ([_]i32{ 1, 2, 7, 100, 1001 }) |rows| {
        for (1..max_bands + 1) |n| {
            try testing.expectEqual(@as(i32, 0), edge(rows, n, 0));
            try testing.expectEqual(rows, edge(rows, n, n));
            for (1..n + 1) |i| try testing.expect(edge(rows, n, i - 1) <= edge(rows, n, i));
        }
    }
}

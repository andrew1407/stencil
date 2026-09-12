//! The raster road as `pipeline.run` drives it (band-parallel through imageRows.zig), plus
//! the codec encode the core never sees. Throughput only: the stages are the core's own
//! micro-benchmarks seen through the adapter, so the ratios are asserted over there.
const std = @import("std");
const core = @import("../core.zig");
const image = @import("../image.zig");
const imageRows = @import("../imageRows.zig");
const fixtures = @import("fixtures.zig");
const timing = @import("timing.zig");

pub const Dims = struct { w: usize, h: usize, lines: usize };

/// Times every stage best-of-3 on one `w`x`h` gradient and prints its throughput.
pub fn run(gpa: std.mem.Allocator, io: std.Io, d: Dims) !void {
    const mp = @as(f64, @floatFromInt(d.w * d.h)) / 1e6;
    const out = std.debug.print;

    const src = try fixtures.gradient(gpa, d.w, d.h);
    defer gpa.free(src);

    // crop: full-image copy, band-parallel as the pipeline drives it
    const CropCtx = struct { src: []const u8, w: i32, h: i32, dst: []u8 };
    const cdst = try gpa.alloc(u8, d.w * d.h * 4);
    defer gpa.free(cdst);
    const crop_ms = timing.bestMs(io, 3, CropCtx{ .src = src, .w = @intCast(d.w), .h = @intCast(d.h), .dst = cdst }, struct {
        fn run(c: CropCtx) void {
            imageRows.crop(c.src, c.w, c.h, .{ .x = 0, .y = 0, .w = c.w, .h = c.h }, c.dst);
        }
    }.run);
    out("  crop        {d:>8.2} ms  ({d:.0} MP/s)\n", .{ crop_ms, mp / crop_ms * 1000 });

    // rotate: one quarter-turn
    const rd = core.rotatedDims(@intCast(d.w), @intCast(d.h), 1);
    const rdst = try gpa.alloc(u8, @as(usize, @intCast(rd.w)) * @as(usize, @intCast(rd.h)) * 4);
    defer gpa.free(rdst);
    const RotCtx = struct { src: []const u8, w: i32, h: i32, dst: []u8 };
    const rot_ms = timing.bestMs(io, 3, RotCtx{ .src = src, .w = @intCast(d.w), .h = @intCast(d.h), .dst = rdst }, struct {
        fn run(c: RotCtx) void {
            imageRows.rotate(c.src, c.w, c.h, 1, c.dst);
        }
    }.run);
    out("  rotate90    {d:>8.2} ms  ({d:.0} MP/s)\n", .{ rot_ms, mp / rot_ms * 1000 });

    // layout: rasterise many polylines (the CLI/pystencil-only path)
    const layer = try gpa.dupe(u8, src);
    defer gpa.free(layer);
    const LineCtx = struct { buf: []u8, w: i32, h: i32, n: usize };
    const ras_ms = timing.bestMs(io, 3, LineCtx{ .buf = layer, .w = @intCast(d.w), .h = @intCast(d.h), .n = d.lines }, struct {
        fn run(c: LineCtx) void {
            var i: usize = 0;
            while (i < c.n) : (i += 1) {
                const bx: f64 = @floatFromInt((i * 37) % 1900);
                const by: f64 = @floatFromInt((i * 53) % 1900);
                const pts = [_]f64{ bx, by, bx + 40, by + 15, bx + 10, by + 60, bx + 70, by + 70 };
                core.rasterizeLine(c.buf, c.w, c.h, .{
                    .points = &pts,
                    .color = "#3366ff",
                    .thickness = 3,
                    .point_size = 4,
                    .style = "solid",
                    .locked = false,
                    .fill_color = "transparent",
                });
            }
        }
    }.run);
    out("  layout      {d:>8.2} ms  ({d:.0} lines/s)\n", .{ ras_ms, @as(f64, @floatFromInt(d.lines)) / ras_ms * 1000 });

    // filter + contour
    const fbuf = try gpa.dupe(u8, src);
    defer gpa.free(fbuf);
    const FiltCtx = struct { buf: []u8, w: i32, h: i32 };
    const filt_ms = timing.bestMs(io, 3, FiltCtx{ .buf = fbuf, .w = @intCast(d.w), .h = @intCast(d.h) }, struct {
        fn run(c: FiltCtx) void {
            imageRows.filter("bw", c.buf, c.w, c.h, .{ .r = 0, .g = 0, .b = 0, .a = 255 });
        }
    }.run);
    out("  filter(bw)  {d:>8.2} ms  ({d:.0} MP/s)\n", .{ filt_ms, mp / filt_ms * 1000 });

    const conbuf = try gpa.dupe(u8, src);
    defer gpa.free(conbuf);
    const ConCtx = struct { gpa: std.mem.Allocator, buf: []u8, w: i32, h: i32 };
    const con_ms = timing.bestMs(io, 3, ConCtx{ .gpa = gpa, .buf = conbuf, .w = @intCast(d.w), .h = @intCast(d.h) }, struct {
        fn run(c: ConCtx) void {
            imageRows.contour(c.gpa, c.buf, c.w, c.h);
        }
    }.run);
    out("  contour     {d:>8.2} ms  ({d:.0} MP/s)\n", .{ con_ms, mp / con_ms * 1000 });

    // encode: the codec step core never sees (adapter cost)
    const img = image.Rgba8{ .width = d.w, .height = d.h, .pixels = src };
    var enc_best: f64 = std.math.floatMax(f64);
    var ei: usize = 0;
    while (ei < 3) : (ei += 1) {
        const t0 = std.Io.Clock.awake.now(io);
        const bytes = try image.encode(gpa, img, .png);
        const ms = timing.elapsedMs(io, t0);
        gpa.free(bytes);
        if (ms < enc_best) enc_best = ms;
    }
    out("  encode(png) {d:>8.2} ms  ({d:.0} MP/s)\n", .{ enc_best, mp / enc_best * 1000 });

    const total = crop_ms + rot_ms + ras_ms + filt_ms + con_ms + enc_best;
    out("  ---------------------------------\n  pipeline    {d:>8.2} ms (sum of stages, best-of-3 each)\n", .{total});
}

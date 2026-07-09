//! Whole-pipeline performance benchmark for the CLI — the adapter-level counterpart to
//! core/tests/bench.test.cpp. The core benches time the shared C++ transforms in
//! isolation; this one times them AS the CLI drives them (through the core.zig ABI
//! wrappers), plus the codec encode step the core never sees, on a large synthetic
//! image. It mirrors the stage order of pipeline.run: crop -> rotate -> draw layout ->
//! filter/contour -> encode.
//!
//! Opt-in and hermetic (no files, no network) — it is NOT part of `zig build test`, so
//! CI never gates on a timing number. Run it explicitly:
//!
//!     zig build bench                 # default 4000x3000, 3000 lines
//!     zig build bench -- 6000 4000 8000   # width height line-count
//!
//! Timings use the monotonic clock via std.Io (no deps). Numbers are informational;
//! watch for order-of-magnitude drift release-over-release, not exact milliseconds.
const std = @import("std");
const core = @import("core.zig");
const image = @import("image.zig");

const Dims = struct { w: usize, h: usize, lines: usize };

// `argv` excludes the program name (just the `--`-forwarded args): width height lines.
fn parseArgs(argv: []const []const u8) Dims {
    var d = Dims{ .w = 4000, .h = 3000, .lines = 3000 };
    if (argv.len > 0) d.w = std.fmt.parseInt(usize, argv[0], 10) catch d.w;
    if (argv.len > 1) d.h = std.fmt.parseInt(usize, argv[1], 10) catch d.h;
    if (argv.len > 2) d.lines = std.fmt.parseInt(usize, argv[2], 10) catch d.lines;
    return d;
}

// A non-flat gradient so filters do real work and contour finds edges.
fn gradient(gpa: std.mem.Allocator, w: usize, h: usize) ![]u8 {
    const buf = try gpa.alloc(u8, w * h * 4);
    for (0..h) |y| {
        for (0..w) |x| {
            const i = (y * w + x) * 4;
            buf[i + 0] = @truncate(x);
            buf[i + 1] = @truncate(y);
            buf[i + 2] = @truncate(x ^ y);
            buf[i + 3] = 255;
        }
    }
    return buf;
}

// Elapsed monotonic milliseconds around a call (Zig 0.16 clocks live on std.Io).
fn elapsedMs(io: std.Io, t0: std.Io.Timestamp) f64 {
    const ns = t0.durationTo(std.Io.Clock.awake.now(io)).toNanoseconds();
    return @as(f64, @floatFromInt(ns)) / 1e6;
}

// Best (min) wall-clock over `reps` runs, in milliseconds — drops scheduler noise.
fn bestMs(io: std.Io, reps: usize, ctx: anytype, comptime run: fn (@TypeOf(ctx)) void) f64 {
    var best: f64 = std.math.floatMax(f64);
    var i: usize = 0;
    while (i < reps) : (i += 1) {
        const t0 = std.Io.Clock.awake.now(io);
        run(ctx);
        const ms = elapsedMs(io, t0);
        if (ms < best) best = ms;
    }
    return best;
}

pub fn main(init: std.process.Init) !void {
    const gpa = init.gpa;
    const io = init.io;
    const arena = init.arena.allocator();
    const argv = try init.minimal.args.toSlice(arena);

    const d = parseArgs(argv[1..]);
    const mp = @as(f64, @floatFromInt(d.w * d.h)) / 1e6;
    const out = std.debug.print;
    out("stencil CLI pipeline bench — {d}x{d} ({d:.1} MP), {d} lines\n", .{ d.w, d.h, mp, d.lines });

    const src = try gradient(gpa, d.w, d.h);
    defer gpa.free(src);

    // ── crop: full-image copy through the core ABI ──────────────────────────────
    const CropCtx = struct { src: []const u8, w: i32, h: i32, dst: []u8 };
    const cdst = try gpa.alloc(u8, d.w * d.h * 4);
    defer gpa.free(cdst);
    const crop_ms = bestMs(io, 3, CropCtx{ .src = src, .w = @intCast(d.w), .h = @intCast(d.h), .dst = cdst }, struct {
        fn run(c: CropCtx) void {
            core.cropImageRGBA(c.src, c.w, c.h, .{ .x = 0, .y = 0, .w = c.w, .h = c.h }, c.dst);
        }
    }.run);
    out("  crop        {d:>8.2} ms  ({d:.0} MP/s)\n", .{ crop_ms, mp / crop_ms * 1000 });

    // ── rotate: one quarter-turn ────────────────────────────────────────────────
    const rd = core.rotatedDims(@intCast(d.w), @intCast(d.h), 1);
    const rdst = try gpa.alloc(u8, @as(usize, @intCast(rd.w)) * @as(usize, @intCast(rd.h)) * 4);
    defer gpa.free(rdst);
    const RotCtx = struct { src: []const u8, w: i32, h: i32, dst: []u8 };
    const rot_ms = bestMs(io, 3, RotCtx{ .src = src, .w = @intCast(d.w), .h = @intCast(d.h), .dst = rdst }, struct {
        fn run(c: RotCtx) void {
            core.rotateImageRGBA(c.src, c.w, c.h, 1, c.dst);
        }
    }.run);
    out("  rotate90    {d:>8.2} ms  ({d:.0} MP/s)\n", .{ rot_ms, mp / rot_ms * 1000 });

    // ── layout: rasterise many polylines (the CLI/pystencil-only path) ──────────
    const layer = try gpa.dupe(u8, src);
    defer gpa.free(layer);
    const LineCtx = struct { buf: []u8, w: i32, h: i32, n: usize };
    const ras_ms = bestMs(io, 3, LineCtx{ .buf = layer, .w = @intCast(d.w), .h = @intCast(d.h), .n = d.lines }, struct {
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
                    .marker_size = 4,
                    .style = "solid",
                    .locked = false,
                    .fill_color = "transparent",
                });
            }
        }
    }.run);
    out("  layout      {d:>8.2} ms  ({d:.0} lines/s)\n", .{ ras_ms, @as(f64, @floatFromInt(d.lines)) / ras_ms * 1000 });

    // ── filter + contour ────────────────────────────────────────────────────────
    const fbuf = try gpa.dupe(u8, src);
    defer gpa.free(fbuf);
    const FiltCtx = struct { gpa: std.mem.Allocator, buf: []u8, n: i32 };
    const filt_ms = bestMs(io, 3, FiltCtx{ .gpa = gpa, .buf = fbuf, .n = @intCast(d.w * d.h) }, struct {
        fn run(c: FiltCtx) void {
            core.applyFilter(c.gpa, "bw", c.buf, c.n, .{ .r = 0, .g = 0, .b = 0, .a = 255 });
        }
    }.run);
    out("  filter(bw)  {d:>8.2} ms  ({d:.0} MP/s)\n", .{ filt_ms, mp / filt_ms * 1000 });

    const conbuf = try gpa.dupe(u8, src);
    defer gpa.free(conbuf);
    const ConCtx = struct { buf: []u8, w: i32, h: i32 };
    const con_ms = bestMs(io, 3, ConCtx{ .buf = conbuf, .w = @intCast(d.w), .h = @intCast(d.h) }, struct {
        fn run(c: ConCtx) void {
            core.applyContour(c.buf, c.w, c.h);
        }
    }.run);
    out("  contour     {d:>8.2} ms  ({d:.0} MP/s)\n", .{ con_ms, mp / con_ms * 1000 });

    // ── encode: the codec step core never sees (adapter cost) ───────────────────
    const img = image.Rgba8{ .width = d.w, .height = d.h, .pixels = src };
    var enc_best: f64 = std.math.floatMax(f64);
    var ei: usize = 0;
    while (ei < 3) : (ei += 1) {
        const t0 = std.Io.Clock.awake.now(io);
        const bytes = try image.encode(gpa, img, .png);
        const ms = elapsedMs(io, t0);
        gpa.free(bytes);
        if (ms < enc_best) enc_best = ms;
    }
    out("  encode(png) {d:>8.2} ms  ({d:.0} MP/s)\n", .{ enc_best, mp / enc_best * 1000 });

    const total = crop_ms + rot_ms + ras_ms + filt_ms + con_ms + enc_best;
    out("  ---------------------------------\n  pipeline    {d:>8.2} ms (sum of stages, best-of-3 each)\n", .{total});
}

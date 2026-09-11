//! Whole-pipeline performance benchmark — the adapter-level counterpart to
//! core/tests/bench.test.cpp, which times the same transforms in isolation. Times the stages
//! pipeline.run composes AS the CLI drives them (band-parallel through imageRows.zig), the
//! codec encode the core never sees, and the adapter paths off the raster road: the scrape
//! HTML walker, the op-plan validator and the line editor's repaint. Opt-in and hermetic (no
//! files, no network) — NOT in `zig build test`: watch for order-of-magnitude drift only.
//!     zig build bench                     # default 4000x3000, 3000 lines
//!     zig build bench -- 6000 4000 8000   # width height line-count
const std = @import("std");
const core = @import("core.zig");
const image = @import("image.zig");
const imageRows = @import("imageRows.zig");
const scrape = @import("scrape.zig");
const llm = @import("llm.zig");
const le = @import("line_edit.zig");
const screen_mod = @import("console/screen.zig");

const Dims = struct { w: usize, h: usize, lines: usize };

// A gallery page with `n` <img> tags plus a CSS background — the shape --source-site scans.
fn galleryHtml(gpa: std.mem.Allocator, n: usize) ![]u8 {
    var b: std.ArrayList(u8) = .empty;
    try b.appendSlice(gpa, "<html><head><base href=\"https://ex.test/g/\"><style>.h{background:url(bg.png)}</style></head><body>");
    for (0..n) |i| {
        var line: [160]u8 = undefined;
        try b.appendSlice(gpa, try std.fmt.bufPrint(&line, "<div class=h><img src=\"p{d}.jpg?v={d}\" alt=\"plate {d} &amp; more\"><source srcset=\"p{d}@2x.webp 2x\"></div>", .{ i, i, i, i }));
    }
    try b.appendSlice(gpa, "</body></html>");
    return b.toOwnedSlice(gpa);
}

// A representative op-plan: reply, a mixed action list and two variants (contract §1–§3).
const plan_json =
    \\{"version":1,"reply":"Cropped, turned and toned.","actions":[{"op":"crop","spec":{"x1":"10%","x2":"-2cm","y1":"0","y2":"90px","aspect":"4:3"}},
    \\{"op":"rotate","dir":"left","times":2},{"op":"filter","mode":"custom","tint":"#A1b2c3"},{"op":"formula","axis":"y","expr":"y*2 + 1"},
    \\{"op":"page","format":"a4"},{"op":"layout","lines":[{"points":[{"x":1,"y":2},{"x":3,"y":4}],"color":"#FF0000","style":"dashed"}]},{"op":"save","name":"plate"}],
    \\"variants":[{"label":"Sepia","actions":[{"op":"filter","mode":"sepia"}]},{"label":"Inverted","actions":[{"op":"filter","mode":"invert"}]}]}
;

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

    // crop: full-image copy, band-parallel as the pipeline drives it
    const CropCtx = struct { src: []const u8, w: i32, h: i32, dst: []u8 };
    const cdst = try gpa.alloc(u8, d.w * d.h * 4);
    defer gpa.free(cdst);
    const crop_ms = bestMs(io, 3, CropCtx{ .src = src, .w = @intCast(d.w), .h = @intCast(d.h), .dst = cdst }, struct {
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
    const rot_ms = bestMs(io, 3, RotCtx{ .src = src, .w = @intCast(d.w), .h = @intCast(d.h), .dst = rdst }, struct {
        fn run(c: RotCtx) void {
            imageRows.rotate(c.src, c.w, c.h, 1, c.dst);
        }
    }.run);
    out("  rotate90    {d:>8.2} ms  ({d:.0} MP/s)\n", .{ rot_ms, mp / rot_ms * 1000 });

    // layout: rasterise many polylines (the CLI/pystencil-only path)
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
    const filt_ms = bestMs(io, 3, FiltCtx{ .buf = fbuf, .w = @intCast(d.w), .h = @intCast(d.h) }, struct {
        fn run(c: FiltCtx) void {
            imageRows.filter("bw", c.buf, c.w, c.h, .{ .r = 0, .g = 0, .b = 0, .a = 255 });
        }
    }.run);
    out("  filter(bw)  {d:>8.2} ms  ({d:.0} MP/s)\n", .{ filt_ms, mp / filt_ms * 1000 });

    const conbuf = try gpa.dupe(u8, src);
    defer gpa.free(conbuf);
    const ConCtx = struct { gpa: std.mem.Allocator, buf: []u8, w: i32, h: i32 };
    const con_ms = bestMs(io, 3, ConCtx{ .gpa = gpa, .buf = conbuf, .w = @intCast(d.w), .h = @intCast(d.h) }, struct {
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
        const ms = elapsedMs(io, t0);
        gpa.free(bytes);
        if (ms < enc_best) enc_best = ms;
    }
    out("  encode(png) {d:>8.2} ms  ({d:.0} MP/s)\n", .{ enc_best, mp / enc_best * 1000 });

    const total = crop_ms + rot_ms + ras_ms + filt_ms + con_ms + enc_best;
    out("  ---------------------------------\n  pipeline    {d:>8.2} ms (sum of stages, best-of-3 each)\n", .{total});

    // The non-raster hot paths: per-call work no pipeline stage touches.
    out("\nadapter paths\n", .{});
    var scratch = std.heap.ArenaAllocator.init(gpa);
    defer scratch.deinit();

    const doc = try galleryHtml(gpa, 400);
    defer gpa.free(doc);
    const HtmlCtx = struct { arena: *std.heap.ArenaAllocator, doc: []const u8 };
    const html_ms = bestMs(io, 5, HtmlCtx{ .arena = &scratch, .doc = doc }, struct {
        fn run(c: HtmlCtx) void {
            _ = c.arena.reset(.retain_capacity);
            _ = scrape.parseMedia(c.arena.allocator(), c.doc, "https://ex.test/g/index.html") catch {};
        }
    }.run);
    _ = scratch.reset(.retain_capacity); // report what was found: a parse that yields
    const found = (try scrape.parseMedia(scratch.allocator(), doc, "https://ex.test/g/index.html")).len; // nothing must not read as fast
    out("  scrapeHtml  {d:>8.2} ms  ({d:.0} KB/s, {d} media from {d} KB)\n", .{ html_ms, @as(f64, @floatFromInt(doc.len)) / 1024 / html_ms * 1000, found, doc.len / 1024 });
    const reps = 2000;
    const PlanCtx = struct { gpa: std.mem.Allocator, n: usize };
    const plan_ms = bestMs(io, 3, PlanCtx{ .gpa = gpa, .n = reps }, struct {
        fn run(c: PlanCtx) void {
            for (0..c.n) |_| {
                var r = llm.parsePlan(c.gpa, plan_json) catch return;
                switch (r) {
                    .plan => |*plan| plan.deinit(),
                    .invalid => |m| c.gpa.free(m),
                }
            }
        }
    }.run);
    out("  opPlan      {d:>8.2} ms  ({d:.1} us/plan, {d} plans)\n", .{ plan_ms, plan_ms * 1000 / reps, reps });
    // Redraw writes real escapes: /dev/null, not a pipe that would measure backpressure.
    const devnull = std.c.open("/dev/null", .{ .ACCMODE = .WRONLY });
    defer _ = std.c.close(devnull);
    var scr = screen_mod.Screen{ .gpa = gpa, .io = io, .fd = devnull, .rows = 40, .cols = 100 };
    defer scr.freeAllForTest();
    var ed = le.Editor{ .fd_in = 0, .fd_out = devnull, .orig = std.mem.zeroes(std.posix.termios), .screen = &scr };
    const long_line = try gpa.alloc(u8, 600);
    defer gpa.free(long_line);
    @memset(long_line, 'x');
    @memcpy(long_line[0..8], "/prompt ");
    const DrawCtx = struct { ed: *le.Editor, line: []const u8, n: usize };
    const draw_ms = bestMs(io, 3, DrawCtx{ .ed = &ed, .line = long_line, .n = reps }, struct {
        fn run(c: DrawCtx) void {
            for (0..c.n) |i| c.ed.refresh("> ", c.line, i % c.line.len);
        }
    }.run);
    out("  redraw      {d:>8.2} ms  ({d:.1} us/repaint, 600-char wrapped line)\n", .{ draw_ms, draw_ms * 1000 / reps });
}

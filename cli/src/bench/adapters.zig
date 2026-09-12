//! The non-raster hot paths: per-call work no pipeline stage touches — the scrape HTML
//! walker, the op-plan validator and the line editor's repaint.
const std = @import("std");
const llm = @import("../llm.zig");
const le = @import("../line_edit.zig");
const scrape = @import("../scrape.zig");
const screen_mod = @import("../console/screen.zig");
const fixtures = @import("fixtures.zig");
const timing = @import("timing.zig");

pub fn run(gpa: std.mem.Allocator, io: std.Io) !void {
    const out = std.debug.print;
    out("\nadapter paths\n", .{});
    var scratch = std.heap.ArenaAllocator.init(gpa);
    defer scratch.deinit();

    const doc = try fixtures.galleryHtml(gpa, 400);
    defer gpa.free(doc);
    const HtmlCtx = struct { arena: *std.heap.ArenaAllocator, doc: []const u8 };
    const html_ms = timing.bestMs(io, 5, HtmlCtx{ .arena = &scratch, .doc = doc }, struct {
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
    const plan_ms = timing.bestMs(io, 3, PlanCtx{ .gpa = gpa, .n = reps }, struct {
        fn run(c: PlanCtx) void {
            for (0..c.n) |_| {
                var r = llm.parsePlan(c.gpa, fixtures.plan_json) catch return;
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
    const long_line = try fixtures.promptLine(gpa, 600);
    defer gpa.free(long_line);
    const DrawCtx = struct { ed: *le.Editor, line: []const u8, n: usize };
    const draw_ms = timing.bestMs(io, 3, DrawCtx{ .ed = &ed, .line = long_line, .n = reps }, struct {
        fn run(c: DrawCtx) void {
            for (0..c.n) |i| c.ed.refresh("> ", c.line, i % c.line.len);
        }
    }.run);
    out("  redraw      {d:>8.2} ms  ({d:.1} us/repaint, 600-char wrapped line)\n", .{ draw_ms, draw_ms * 1000 / reps });
}

//! The non-raster hot paths: per-call work no pipeline stage touches — the scrape HTML
//! walker, the op-plan validator and the line editor's repaint. Each runs at TWO input
//! sizes, because the only thing worth asserting is how the cost scales between them
//! (timing.Ratios); the us/call printed alongside is a drift reference, never a gate.
const std = @import("std");
const llm = @import("../llm.zig");
const le = @import("../line_edit.zig");
const scrape = @import("../scrape.zig");
const scriptCore = @import("../scriptCore.zig");
const emit = @import("../script/emit.zig");
const screen_mod = @import("../console/screen.zig");
const fixtures = @import("fixtures.zig");
const timing = @import("timing.zig");

const page_url = "https://ex.test/g/index.html";

/// The walker is one pass over the document, so 4x the tags must cost about 4x.
fn scrapeHtml(gpa: std.mem.Allocator, io: std.Io, r: *timing.Ratios) !void {
    var scratch = std.heap.ArenaAllocator.init(gpa);
    defer scratch.deinit();
    const small = try fixtures.galleryHtml(gpa, 100);
    defer gpa.free(small);
    const big = try fixtures.galleryHtml(gpa, 400);
    defer gpa.free(big);

    const Ctx = struct { arena: *std.heap.ArenaAllocator, doc: []const u8 };
    const walk = struct {
        fn run(c: Ctx) void {
            _ = c.arena.reset(.retain_capacity);
            _ = scrape.parseMedia(c.arena.allocator(), c.doc, page_url) catch {};
        }
    }.run;

    // Report the yield too: a parse that finds nothing would otherwise read as fast.
    _ = scratch.reset(.retain_capacity);
    const found = (try scrape.parseMedia(scratch.allocator(), big, page_url)).len;
    std.debug.print("  scrapeHtml — {d} media from {d} KB\n", .{ found, big.len / 1024 });

    const small_us = timing.perCallUs(io, "scrapeHtml (100 tags)", 400, Ctx{ .arena = &scratch, .doc = small }, walk);
    const big_us = timing.perCallUs(io, "scrapeHtml (400 tags)", 100, Ctx{ .arena = &scratch, .doc = big }, walk);
    r.under("scrapeHtml", big_us, small_us, 8, "4x the tags: the walker is one pass");
}

/// Every fixture in the corpus lands here, and a plan fans out to variants — so validation
/// must stay LINEAR in the point count it re-parses and re-serializes.
fn opPlan(gpa: std.mem.Allocator, io: std.Io, r: *timing.Ratios) !void {
    const small = try fixtures.layoutPlan(gpa, 32);
    defer gpa.free(small);
    const big = try fixtures.layoutPlan(gpa, 256);
    defer gpa.free(big);

    const Ctx = struct { gpa: std.mem.Allocator, json: []const u8 };
    const parse = struct {
        fn run(c: Ctx) void {
            var res = llm.parsePlan(c.gpa, c.json) catch return;
            switch (res) {
                .plan => |*plan| plan.deinit(),
                .invalid => |m| c.gpa.free(m),
            }
        }
    }.run;

    _ = timing.perCallUs(io, "parsePlan (7 actions + 2 variants)", 2000, Ctx{ .gpa = gpa, .json = fixtures.plan_json }, parse);
    const small_us = timing.perCallUs(io, "parsePlan (layout, 32 points)", 2000, Ctx{ .gpa = gpa, .json = small }, parse);
    const big_us = timing.perCallUs(io, "parsePlan (layout, 256 points)", 500, Ctx{ .gpa = gpa, .json = big }, parse);
    r.under("parsePlan", big_us, small_us, 16, "8x the points stays linear");
}

/// `refresh` paints at most `Screen.maxPromptRows` (8) rows however long the input grows, so
/// a 10x longer line must cost barely more. If it tracks the input, the cap regressed.
fn redraw(gpa: std.mem.Allocator, io: std.Io, r: *timing.Ratios) !void {
    // Real escapes go to /dev/null, not a pipe that would measure backpressure instead.
    const devnull = std.c.open("/dev/null", .{ .ACCMODE = .WRONLY });
    defer _ = std.c.close(devnull);
    var scr = screen_mod.Screen{ .gpa = gpa, .io = io, .fd = devnull, .rows = 40, .cols = 100 };
    defer scr.freeAllForTest();
    var ed = le.Editor{ .fd_in = 0, .fd_out = devnull, .orig = std.mem.zeroes(std.posix.termios), .screen = &scr };

    const short = try fixtures.promptLine(gpa, 600); // 7 wrapped rows — under the cap
    defer gpa.free(short);
    const long = try fixtures.promptLine(gpa, 6000); // 61 rows — clamped to 8
    defer gpa.free(long);

    const Ctx = struct { ed: *le.Editor, line: []const u8 };
    const paint = struct {
        var tick: usize = 0;
        fn run(c: Ctx) void {
            tick +%= 1;
            c.ed.refresh("> ", c.line, tick % c.line.len);
        }
    }.run;

    const short_us = timing.perCallUs(io, "redraw (600-char line, 7 rows)", 2000, Ctx{ .ed = &ed, .line = short }, paint);
    const long_us = timing.perCallUs(io, "redraw (6000-char line, capped)", 2000, Ctx{ .ed = &ed, .line = long }, paint);
    r.under("redraw", long_us, short_us, 3, "10x the input, same 8-row cap");
}

/// `--script-emit` walks the lowered stream once and writes text, so 8x the ops costs about
/// 8x — and the walk must stay cheaper than the parse that produced the stream.
fn scriptEmit(gpa: std.mem.Allocator, io: std.Io, r: *timing.Ratios) !void {
    const small_src = try fixtures.script(gpa, 32);
    defer gpa.free(small_src);
    const big_src = try fixtures.script(gpa, 256);
    defer gpa.free(big_src);

    var small = try scriptCore.Script.parse(small_src);
    defer small.deinit();
    var big = try scriptCore.Script.parse(big_src);
    defer big.deinit();

    const Ctx = struct { gpa: std.mem.Allocator, script: scriptCore.Script };
    const render = struct {
        fn run(c: Ctx) void {
            var bad: emit.Refusal = .{};
            const text = emit.renderAlloc(c.gpa, c.script, .py, "bench.stc", &bad) catch return;
            c.gpa.free(text);
        }
    }.run;
    const Source = struct { text: []const u8 };
    const parse = struct {
        fn run(c: Source) void {
            var s = scriptCore.Script.parse(c.text) catch return;
            s.deinit();
        }
    }.run;

    const small_us = timing.perCallUs(io, "scriptEmit (32 shapes)", 2000, Ctx{ .gpa = gpa, .script = small }, render);
    const big_us = timing.perCallUs(io, "scriptEmit (256 shapes)", 500, Ctx{ .gpa = gpa, .script = big }, render);
    r.under("scriptEmit", big_us, small_us, 16, "8x the ops: one pass over the stream");

    const parse_us = timing.perCallUs(io, "scriptParse (256 shapes)", 500, Source{ .text = big_src }, parse);
    r.under("scriptEmit vs parse", big_us, parse_us, 1, "emitting costs less than parsing");
}

pub fn run(gpa: std.mem.Allocator, io: std.Io, r: *timing.Ratios) !void {
    std.debug.print("\nadapter paths\n", .{});
    try scrapeHtml(gpa, io, r);
    try opPlan(gpa, io, r);
    try redraw(gpa, io, r);
    try scriptEmit(gpa, io, r);
}

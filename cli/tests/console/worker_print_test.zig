//! A worker's `error:` lines during a watched call in the full-screen console, while the terminal's
//! thread beats the spinner: held off the Screen until that thread's next tick, then landed there
//! in the order said — the Screen and its sink are only ever touched by the thread that owns them.
const std = @import("std");
const logo = @import("../../src/app/logo.zig");
const report = @import("../../src/app/report.zig");
const job = @import("../../src/net/job.zig");
const jobCall = @import("../../src/net/jobCall.zig");
const screen = @import("../../src/console/screen.zig");
const spinner = @import("../../src/console/render/spinner.zig");
const testing = std.testing;

const Rig = struct {
    io: std.Io,
    scr: *screen.Screen,
    spin: *spinner.Spinner,
    owner: std.Thread.Id,
    ticks: std.atomic.Value(usize) = .init(0),
    clock: i64 = 0,
    sink_calls: usize = 0,
    foreign_calls: usize = 0,

    fn sink(ctx: *anyopaque, bytes: []const u8) void {
        const self: *Rig = @ptrCast(@alignCast(ctx));
        self.sink_calls += 1;
        if (std.Thread.getCurrentId() != self.owner) self.foreign_calls += 1;
        screen.Screen.sinkTrampoline(self.scr, bytes);
    }

    fn poll(ctx: *anyopaque, _: i32) bool {
        const self: *Rig = @ptrCast(@alignCast(ctx));
        self.io.sleep(.fromMilliseconds(1), .awake) catch {};
        return false;
    }

    // Each beat is one spinner frame later, so every beat before the first line lands repaints the row.
    fn beat(ctx: *anyopaque, _: i64) void {
        const self: *Rig = @ptrCast(@alignCast(ctx));
        self.clock += spinner.frame_ms;
        self.spin.beat(self.clock);
        _ = self.ticks.fetchAdd(1, .release);
    }

    fn waitTicks(self: *Rig, n: usize) void {
        while (self.ticks.load(.acquire) < n) self.io.sleep(.fromMilliseconds(1), .awake) catch {};
    }

    fn work(self: *Rig) void {
        self.waitTicks(3);
        report.err("first\n", .{});
        self.waitTicks(6);
        report.err("second\n", .{});
    }
};

test "a worker's lines wait for the terminal's thread, which lands them in order past the spinner" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    logo.init(false, false);
    defer logo.init(false, false);
    var s = screen.Screen{ .gpa = a, .io = io, .fd = -1, .rows = 10, .cols = 60 };
    defer s.freeAllForTest();
    s.setRevealSpeed(1.0);
    s.installForTest();
    defer s.uninstallForTest();
    var spin = spinner.Spinner{};
    var rig = Rig{ .io = io, .scr = &s, .spin = &spin, .owner = std.Thread.getCurrentId() };
    logo.setSink(Rig.sink, &rig);

    spin.start("thinking…");
    defer spin.stop();
    try testing.expectEqual(@as(usize, 1), s.lines.len);
    const w = job.Waiter{ .ctx = &rig, .poll = Rig.poll, .beat_ctx = &rig, .beat = Rig.beat };
    switch (jobCall.watched(io, w, Rig.work, .{&rig})) {
        .done => {},
        .cancelled, .timed_out => return error.TestUnexpectedResult,
    }

    // The beats turned the spinner while the worker was live, and every sink call came from here.
    try testing.expect(spin.frame >= 1);
    try testing.expect(rig.sink_calls >= 2);
    try testing.expectEqual(@as(usize, 0), rig.foreign_calls);
    // The pre-print hook erased the spinner before the first line; the last line landed as the
    // call ended, before the caller said anything of its own.
    try testing.expectEqual(@as(usize, 2), s.lines.len);
    try testing.expectEqualStrings("error: first", s.lines.at(0));
    try testing.expectEqualStrings("error: second", s.lines.at(1));
    logo.print("done\n", .{});
    try testing.expectEqualStrings("done", s.lines.at(2));
}

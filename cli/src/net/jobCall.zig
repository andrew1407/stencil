//! The one watched worker: a call — an HTTP exchange, an LLM turn, the events socket's opening —
//! on a worker while the caller watches its terminal (job.zig's Waiter). A Ctrl-C or the watch's
//! deadline stops the worker at its next Io call and waits for it: nothing it borrowed outlives the wait.
const std = @import("std");
const job = @import("job.zig");
const report = @import("../app/report.zig");

/// What a watched call came to: its own result, or how the watch ended it with whatever the
/// stopped call returned — a body or a connection it had just got is the caller's to free.
pub fn Outcome(comptime R: type) type {
    return union(enum) { done: R, cancelled: R, timed_out: R };
}

fn Result(comptime func: anytype) type {
    return @typeInfo(@TypeOf(func)).@"fn".return_type.?;
}

/// `func(args)` watched by `waiter`, else by the thread's watch; with neither, a plain call here.
pub fn call(io: std.Io, waiter: ?job.Waiter, comptime func: anytype, args: std.meta.ArgsTuple(@TypeOf(func))) Outcome(Result(func)) {
    const w = waiter orelse job.threadWatch() orelse return .{ .done = @call(.auto, func, args) };
    return watched(io, w, func, args);
}

/// `func(args)` on a worker `w` watches; with no worker to spare, a plain call here.
pub fn watched(io: std.Io, w: job.Waiter, comptime func: anytype, args: std.meta.ArgsTuple(@TypeOf(func))) Outcome(Result(func)) {
    const Task = struct {
        finished: std.atomic.Value(bool) = .init(false),

        fn run(self: *@This(), a: std.meta.ArgsTuple(@TypeOf(func))) Result(func) {
            defer self.finished.store(true, .release);
            return @call(.auto, func, a);
        }
    };
    var task: Task = .{};
    var fut = io.concurrent(Task.run, .{ &task, args }) catch return .{ .done = @call(.auto, func, args) };
    defer report.flushDeferred(); // the worker's last words land before the caller's own
    const started = std.Io.Clock.now(.awake, io).toMilliseconds();
    while (!task.finished.load(.acquire)) {
        if (w.poll(w.ctx, job.beat_ms)) {
            if (task.finished.load(.acquire)) break; // it landed in the same instant: keep the answer
            return .{ .cancelled = fut.cancel(io) };
        }
        report.flushDeferred();
        const now = std.Io.Clock.now(.awake, io).toMilliseconds();
        if (w.beat) |b| if (w.beat_ctx) |c| b(c, now);
        if (w.timeout_ms > 0 and now - started > w.timeout_ms) {
            if (task.finished.load(.acquire)) break;
            return .{ .timed_out = fut.cancel(io) };
        }
    }
    return .{ .done = fut.await(io) };
}

const testing = std.testing;

const Presses = struct {
    left: usize,
    beats: usize = 0,
    fn poll(ctx: *anyopaque, _: i32) bool {
        const self: *Presses = @ptrCast(@alignCast(ctx));
        if (self.left == 0) return true;
        self.left -= 1;
        return false;
    }
    fn beat(ctx: *anyopaque, _: i64) void {
        const self: *Presses = @ptrCast(@alignCast(ctx));
        self.beats += 1;
    }
};

fn nap(io: std.Io, ms: i64) std.Io.Cancelable!i64 {
    try io.sleep(.fromMilliseconds(ms), .awake);
    return ms;
}

test "call: a Ctrl-C stops the call at once; a quick one, or one with no watch, keeps its answer" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var presses = Presses{ .left = 2 };
    const t0 = std.Io.Clock.now(.awake, io).toMilliseconds();
    const w = job.Waiter{ .ctx = &presses, .poll = Presses.poll, .beat_ctx = &presses, .beat = Presses.beat };
    const stopped = call(io, w, nap, .{ io, 20_000 });
    try testing.expectError(error.Canceled, stopped.cancelled);
    try testing.expect(std.Io.Clock.now(.awake, io).toMilliseconds() - t0 < 5_000); // not the 20 s nap
    try testing.expectEqual(@as(usize, 2), presses.beats); // a beat per quiet poll, none for the press

    presses.left = std.math.maxInt(usize); // a watch that never fires (nor waits: this one spins)
    try testing.expectEqual(@as(i64, 5), try call(io, .{ .ctx = &presses, .poll = Presses.poll }, nap, .{ io, 5 }).done);
    try testing.expectEqual(@as(i64, 1), try call(io, null, nap, .{ io, 1 }).done);
}

fn waitBeat(ctx: *anyopaque, timeout_ms: i32) bool {
    const io: *const std.Io = @ptrCast(@alignCast(ctx));
    io.sleep(.fromMilliseconds(timeout_ms), .awake) catch {};
    return false;
}

test "call: the watch's own deadline stops a call that outlives it" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    var io = threaded.io();
    const w = job.Waiter{ .ctx = @ptrCast(&io), .poll = waitBeat, .timeout_ms = 150 };
    const t0 = std.Io.Clock.now(.awake, io).toMilliseconds();
    try testing.expectError(error.Canceled, watched(io, w, nap, .{ io, 20_000 }).timed_out);
    try testing.expect(std.Io.Clock.now(.awake, io).toMilliseconds() - t0 < 5_000);
}

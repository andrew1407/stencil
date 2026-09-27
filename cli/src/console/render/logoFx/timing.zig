//! Frame pacing for the console animations: each wait sends the frame drawn so far, then
//! blocks on the input tty until the frame's deadline, so a keystroke cuts an animation short
//! instead of being swallowed by it — and nothing spins while it waits.
const std = @import("std");
const screen_mod = @import("../../screen.zig");
const frame = @import("../../screen/frame.zig");
const Screen = screen_mod.Screen;

/// A stand-in clock for tests: each frame's wait goes here instead of the terminal pacing.
/// `abortable` is false only for the press hold; the answer is whether input cut it short.
pub const Pacer = struct { ctx: *anyopaque, wait: *const fn (ctx: *anyopaque, ms: i64, abortable: bool) bool };
pub var pacer: ?Pacer = null;

/// Wait one frame of `ms`; true if a keystroke is queued (skips the rest of the effect).
pub fn waitFrame(self: *Screen, ms: i64) bool {
    frame.flushActive();
    if (pacer) |p| return p.wait(p.ctx, ms, true);
    return waitUntil(self.io, self.in_fd, deadline(self.io, ms));
}

/// Hold `ms` whatever is typed — the press frame, whose own mouse release is already queued
/// by the time it is drawn (so aborting on input would skip it).
pub fn hold(self: *Screen, ms: i64) void {
    frame.flushActive();
    if (pacer) |p| {
        _ = p.wait(p.ctx, ms, false);
        return;
    }
    _ = waitUntil(self.io, null, deadline(self.io, ms));
}

fn deadline(io: std.Io, ms: i64) i96 {
    return std.Io.Clock.now(.awake, io).nanoseconds + @as(i96, ms) * std.time.ns_per_ms;
}

// Block on `fd` (or sleep, with none) until `due`; poll's millisecond timeout is re-armed
// until the deadline has really passed, so a frame never lands before its tick.
fn waitUntil(io: std.Io, fd: ?std.posix.fd_t, due: i96) bool {
    while (true) {
        const left = due - std.Io.Clock.now(.awake, io).nanoseconds;
        if (left <= 0) return false;
        const ms: i32 = @intCast(@min(@divFloor(left + std.time.ns_per_ms - 1, std.time.ns_per_ms), std.math.maxInt(i32)));
        var pfd = [_]std.posix.pollfd{.{ .fd = fd orelse -1, .events = std.posix.POLL.IN, .revents = 0 }};
        const ready = std.posix.poll(&pfd, ms) catch return false;
        if (ready > 0 and fd != null) return true;
    }
}

const testing = std.testing;

test "waitFrame: a queued key ends the wait at once; with none it lasts the frame, never less" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const p = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(p[0]);
    defer _ = std.c.close(p[1]);
    var s = Screen{ .gpa = testing.allocator, .io = io, .fd = -1, .in_fd = p[0], .rows = 10, .cols = 40 };
    defer s.freeAll();

    const t0 = std.Io.Clock.now(.awake, io).nanoseconds;
    try testing.expect(!waitFrame(&s, 15));
    try testing.expect(std.Io.Clock.now(.awake, io).nanoseconds - t0 >= 15 * std.time.ns_per_ms);

    _ = std.c.write(p[1], "k", 1);
    const t1 = std.Io.Clock.now(.awake, io).nanoseconds;
    try testing.expect(waitFrame(&s, 5000));
    try testing.expect(std.Io.Clock.now(.awake, io).nanoseconds - t1 < 1000 * std.time.ns_per_ms);

    const t2 = std.Io.Clock.now(.awake, io).nanoseconds;
    hold(&s, 12); // the key is still queued, and a hold does not care
    try testing.expect(std.Io.Clock.now(.awake, io).nanoseconds - t2 >= 12 * std.time.ns_per_ms);
}

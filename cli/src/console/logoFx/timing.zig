//! Frame pacing for the console animations: busy-waits that stay honest about queued
//! input, so a keystroke cuts an animation short instead of being swallowed by it.
const std = @import("std");
const logo = @import("../../logo.zig");
const ansi = @import("../ansi.zig");
const screen_mod = @import("../screen.zig");
const Screen = screen_mod.Screen;
const Span = ansi.Span;

// Busy-wait ~`ms` (a plain nanosleep is coalesced by the io loop, breaking frame pacing),
// but return true early the instant input is waiting — a queued click/keystroke supersedes.
pub fn sleepOrAbort(self: *Screen, ms: i64) bool {
    const deadline: i96 = std.Io.Clock.now(.awake, self.io).nanoseconds + @as(i96, ms) * std.time.ns_per_ms;
    while (std.Io.Clock.now(.awake, self.io).nanoseconds < deadline) {
        if (inputPending(self)) return true;
    }
    return false;
}

// Busy-wait ~`ms`, ignoring queued input — for the press frame, whose own trailing mouse
// release is already waiting by the time it is drawn (so aborting on input would skip it).
pub fn spin(io: std.Io, ms: i64) void {
    const deadline: i96 = std.Io.Clock.now(.awake, io).nanoseconds + @as(i96, ms) * std.time.ns_per_ms;
    while (std.Io.Clock.now(.awake, io).nanoseconds < deadline) {}
}

// Whether the input tty has a byte ready to read right now (non-blocking poll). False when no
// input fd was wired up (e.g. tests) so the flourish just plays to completion.
pub fn inputPending(self: *Screen) bool {
    const fd = self.in_fd orelse return false;
    var pfd = [_]std.posix.pollfd{.{ .fd = fd, .events = std.posix.POLL.IN, .revents = 0 }};
    const ready = std.posix.poll(&pfd, 0) catch return false;
    return ready > 0;
}

// Wait one frame; true if a keystroke is queued (skips the rest of the sweep). Short
// frames spin for exact pacing; long ones block on the input poll instead of burning a core.
pub fn waitFrame(self: *Screen, ms: i64) bool {
    if (ms <= 20) return sleepOrAbort(self, ms);
    const fd = self.in_fd orelse return sleepOrAbort(self, ms);
    var pfd = [_]std.posix.pollfd{.{ .fd = fd, .events = std.posix.POLL.IN, .revents = 0 }};
    const ready = std.posix.poll(&pfd, @intCast(@min(ms, @as(i64, std.math.maxInt(i32))))) catch return false;
    return ready > 0;
}

/// A batching writer: one animation frame goes out as a single tty write.
pub const Frame = struct {
    fd: std.posix.fd_t,
    buf: [16384]u8 = undefined,
    len: usize = 0,

    pub fn put(self: *Frame, bytes: []const u8) void {
        if (self.len + bytes.len > self.buf.len) self.flush();
        if (bytes.len > self.buf.len) { // never fits — write it straight through
            screen_mod.ttyWrite(self.fd, bytes);
            return;
        }
        @memcpy(self.buf[self.len..][0..bytes.len], bytes);
        self.len += bytes.len;
    }

    pub fn at(self: *Frame, row: u16) void {
        var b: [16]u8 = undefined;
        self.put(std.fmt.bufPrint(&b, "\x1b[{d};1H", .{row}) catch return);
    }

    pub fn flush(self: *Frame) void {
        if (self.len == 0) return;
        screen_mod.ttyWrite(self.fd, self.buf[0..self.len]);
        self.len = 0;
    }
};

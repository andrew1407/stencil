//! The watch a caller keeps on its terminal while a slow call runs on a worker (jobCall.zig):
//! what to poll for a Ctrl-C, the beat a spinner advances on and an overall deadline. A thread
//! may install one for every call that brings none — the console's input loop does.
const std = @import("std");

/// `poll` blocks up to `timeout_ms` and answers whether to give up.
pub const Waiter = struct {
    ctx: *anyopaque,
    poll: *const fn (ctx: *anyopaque, timeout_ms: i32) bool,
    /// Fired once per beat that did not give up, with the clock in ms — a spinner advances on it.
    beat_ctx: ?*anyopaque = null,
    beat: ?*const fn (ctx: *anyopaque, now_ms: i64) void = null,
    /// ms the whole wait may take; 0 = only the call's own deadline bounds it.
    timeout_ms: i64 = 0,
};

/// How long one wait beat blocks on the watch — short enough that Ctrl-C feels immediate.
pub const beat_ms: i32 = 60;

/// The watch a call that brings none runs under, per thread: the console's terminal on its
/// input loop's thread (console/netWait.zig); null = block, as a one-shot run does.
threadlocal var thread_watch: ?Waiter = null;

/// Put `w` in force for this thread's unwatched calls; returns the one it replaces.
pub fn watchThread(w: ?Waiter) ?Waiter {
    const prev = thread_watch;
    thread_watch = w;
    return prev;
}

pub fn threadWatch() ?Waiter {
    return thread_watch;
}

const testing = std.testing;

fn never(_: *anyopaque, _: i32) bool {
    return false;
}

test "watchThread installs a watch for this thread only, and hands back the one it replaced" {
    var dummy: u8 = 0;
    try testing.expect(watchThread(.{ .ctx = &dummy, .poll = never }) == null);
    const Other = struct {
        fn seen(out: *bool) void {
            out.* = threadWatch() != null;
        }
    };
    var other_sees = true;
    const t = try std.Thread.spawn(.{}, Other.seen, .{&other_sees});
    t.join();
    try testing.expect(!other_sees); // a worker never inherits the console's watch
    try testing.expect(watchThread(null) != null);
    try testing.expect(threadWatch() == null);
}

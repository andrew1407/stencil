//! What another thread says while the terminal's thread owns the human channel (a sink installed or
//! a pre-print hook armed): held whole, in the order said, until the owner flushes it
//! (logo.flushDeferred). The lock guards only this state; no callback ever runs inside it.
const std = @import("std");

pub const gpa = std.heap.page_allocator;

/// Bytes held for the owner at most; a print past it is dropped rather than growing without end.
pub const max_bytes = 1 << 20;

var busy: std.atomic.Value(bool) = .init(false);
var owner: std.atomic.Value(u64) = .init(0); // 0 = nobody owns the channel
var queue: std.ArrayList(u8) = .empty;

fn lock() void {
    while (busy.cmpxchgWeak(false, true, .acquire, .monotonic) != null) std.atomic.spinLoopHint();
}

fn unlock() void {
    busy.store(false, .release);
}

fn thisThread() u64 {
    return @intCast(std.Thread.getCurrentId());
}

/// True on the thread that owns the channel.
pub fn mine() bool {
    return owner.load(.acquire) == thisThread();
}

/// True while some thread owns the channel.
pub fn owned() bool {
    return owner.load(.acquire) != 0;
}

/// Make this thread the owner: every other thread's print is held for it from now on.
pub fn own() void {
    lock();
    defer unlock();
    owner.store(thisThread(), .release);
}

/// Hold `bytes` for the owner; false when no other thread owns the channel, so the caller prints.
pub fn push(bytes: []const u8) bool {
    lock();
    defer unlock();
    const o = owner.load(.monotonic);
    if (o == 0 or o == thisThread()) return false;
    if (queue.items.len + bytes.len <= max_bytes) queue.appendSlice(gpa, bytes) catch {};
    return true;
}

/// Everything held so far, for the owner to emit and then `deinit(gpa)`; `release` also gives up
/// ownership under the same lock, so nothing can be held after it. Empty on any other thread.
pub fn take(release: bool) std.ArrayList(u8) {
    lock();
    defer unlock();
    if (owner.load(.monotonic) != thisThread()) return .empty;
    if (release) owner.store(0, .release);
    const held = queue;
    queue = .empty;
    return held;
}

const testing = std.testing;

test "only another thread's bytes are held, whole and in order, until the owner takes them" {
    try testing.expect(!push("nobody owns the channel\n"));
    own();
    try testing.expect(mine());
    try testing.expect(!push("the owner prints for itself\n"));
    const Worker = struct {
        fn say(held: *[2]bool) void {
            held[0] = push("error: first\n");
            held[1] = push("error: second\n");
        }
    };
    var held = [2]bool{ false, false };
    const t = try std.Thread.spawn(.{}, Worker.say, .{&held});
    t.join();
    try testing.expect(held[0] and held[1]);
    var got = take(true);
    defer got.deinit(gpa);
    try testing.expectEqualStrings("error: first\nerror: second\n", got.items);
    try testing.expect(!mine() and !owned()); // released: the next worker prints straight to stderr again
    var none = take(false);
    try testing.expectEqual(@as(usize, 0), none.items.len);
    none.deinit(gpa);
}

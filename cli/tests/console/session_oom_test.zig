//! A session edit that runs out of memory part-way frees what it took exactly once: every
//! allocation in the edit is failed in turn under the leak-checking allocator. The view rebuild
//! is best-effort and swallows its own failures, so checkAllAllocationFailures cannot drive it.
const std = @import("std");
const Session = @import("../../src/console/session.zig").Session;
const testing = std.testing;

fn filterTwice(a: std.mem.Allocator) !void {
    var session: Session = .{ .gpa = a };
    defer session.deinit();
    const px = try a.alloc(u8, 2 * 2 * 4);
    @memset(px, 9);
    try session.loadImage(.{ .width = 2, .height = 2, .pixels = px }, "oom", true, .png, null);
    try session.setFilter("sepia", "");
    try session.setFilter("custom", "#7c3aed");
}

test "setFilter: an allocation failing anywhere in it frees each string once" {
    var i: usize = 0;
    while (true) : (i += 1) {
        var failing = std.testing.FailingAllocator.init(testing.allocator, .{ .fail_index = i });
        filterTwice(failing.allocator()) catch |e| {
            try testing.expectEqual(error.OutOfMemory, e);
            continue;
        };
        if (!failing.has_induced_failure) break;
    }
}

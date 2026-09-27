//! Writing to the terminal: the retrying raw write every frame goes through, cursor
//! placement, and splitting arriving output into scrollback lines.
const std = @import("std");
const frame = @import("frame.zig");

/// A stand-in terminal for tests: writes to `fd` land in `write` instead of the tty.
pub const Tap = struct { fd: std.posix.fd_t, ctx: *anyopaque, write: *const fn (ctx: *anyopaque, bytes: []const u8) void };
pub var tap: ?Tap = null;

/// Write to the terminal: into the open frame when one is being drawn, else straight out.
pub fn ttyWrite(fd: std.posix.fd_t, bytes: []const u8) void {
    if (frame.capture(fd, bytes)) return;
    rawWrite(fd, bytes);
}

/// libc write to a raw fd (std.posix.write is unavailable here the same way line_edit uses).
pub fn rawWrite(fd: std.posix.fd_t, bytes: []const u8) void {
    if (tap) |t| if (t.fd == fd) return t.write(t.ctx, bytes);
    var i: usize = 0;
    while (i < bytes.len) {
        const n = std.c.write(fd, bytes[i..].ptr, bytes.len - i);
        if (n <= 0) return;
        i += @intCast(n);
    }
}

/// Move the cursor to (row,1) without clearing — for in-place overwrites during the animation.
pub fn gotoRow(fd: std.posix.fd_t, row: u16) void {
    var b: [16]u8 = undefined;
    const s = std.fmt.bufPrint(&b, "\x1b[{d};1H", .{row}) catch return;
    ttyWrite(fd, s);
}

/// Move the cursor to (row,1) and clear the whole line.
pub fn gotoClear(fd: std.posix.fd_t, row: u16) void {
    var b: [24]u8 = undefined;
    const s = std.fmt.bufPrint(&b, "\x1b[{d};1H\x1b[2K", .{row}) catch return;
    ttyWrite(fd, s);
}

/// The scrollback's owned lines: a ring, so the oldest leave off its front without the rest moving.
pub const Lines = std.Deque([]u8);

/// Append `bytes` to `pending`, flushing a completed owned line into `dst` (the scrollback's `Lines`
/// or a header's list) on each '\n'; a CRLF's '\r' is dropped. Allocation failures drop the line.
pub fn pushChunk(gpa: std.mem.Allocator, dst: anytype, pending: *std.ArrayList(u8), bytes: []const u8) void {
    for (bytes) |ch| {
        if (ch == '\n') {
            var line = pending.items;
            if (line.len != 0 and line[line.len - 1] == '\r') line = line[0 .. line.len - 1];
            const owned = gpa.dupe(u8, line) catch {
                pending.clearRetainingCapacity();
                continue;
            };
            (if (@TypeOf(dst) == *Lines) dst.pushBack(gpa, owned) else dst.append(gpa, owned)) catch gpa.free(owned);
            pending.clearRetainingCapacity();
        } else {
            pending.append(gpa, ch) catch {};
        }
    }
}

/// Put `line` at `i`, the lines from there on moving one down — a wrapped line's tail, so only
/// what just arrived moves.
pub fn insertAt(lines: *Lines, gpa: std.mem.Allocator, i: usize, line: []u8) !void {
    try lines.pushBack(gpa, line);
    var j = lines.len - 1;
    while (j > i) : (j -= 1) lines.atPtr(j).* = lines.at(j - 1);
    lines.atPtr(i).* = line;
}

/// Take line `i` out, the lines after it moving one up; the caller owns it.
pub fn removeAt(lines: *Lines, i: usize) []u8 {
    const line = lines.at(i);
    for (i..lines.len - 1) |j| lines.atPtr(j).* = lines.at(j + 1);
    lines.len -= 1;
    return line;
}

/// Free every line and empty the ring, keeping its storage.
pub fn clearLines(lines: *Lines, gpa: std.mem.Allocator) void {
    while (lines.popFront()) |l| gpa.free(l);
    lines.head = 0;
}

const testing = std.testing;

test "pushChunk: splits on newline, drops CR, buffers partials" {
    var lines: std.ArrayList([]u8) = .empty;
    var pending: std.ArrayList(u8) = .empty;
    defer {
        for (lines.items) |l| testing.allocator.free(l);
        lines.deinit(testing.allocator);
        pending.deinit(testing.allocator);
    }
    pushChunk(testing.allocator, &lines, &pending, "one\r\ntwo\n");
    pushChunk(testing.allocator, &lines, &pending, "par");
    pushChunk(testing.allocator, &lines, &pending, "tial\n");
    try testing.expectEqual(@as(usize, 3), lines.items.len);
    try testing.expectEqualStrings("one", lines.items[0]);
    try testing.expectEqualStrings("two", lines.items[1]);
    try testing.expectEqualStrings("partial", lines.items[2]);
}

test "Lines: a wrapped tail goes in behind its head, a notice comes out, and the ring wraps" {
    const a = testing.allocator;
    var lines: Lines = .empty;
    defer {
        clearLines(&lines, a);
        lines.deinit(a);
    }
    var pending: std.ArrayList(u8) = .empty;
    defer pending.deinit(a);
    pushChunk(a, &lines, &pending, "one\nthree\n");
    try insertAt(&lines, a, 1, try a.dupe(u8, "two"));
    try insertAt(&lines, a, 3, try a.dupe(u8, "four"));
    for ([_][]const u8{ "one", "two", "three", "four" }, 0..) |want, i| try testing.expectEqualStrings(want, lines.at(i));
    a.free(removeAt(&lines, 1));
    try testing.expectEqualStrings("three", lines.at(1));
    try testing.expectEqual(@as(usize, 3), lines.len);
    // Past the end of its storage the ring starts over at the front: dropping the oldest moves nothing.
    const cap = lines.buffer.len;
    a.free(lines.popFront().?);
    while (lines.len < cap) pushChunk(a, &lines, &pending, "more\n");
    try testing.expectEqual(cap, lines.buffer.len);
    try testing.expect(lines.head != 0);
    try testing.expectEqualStrings("three", lines.at(0));
    try testing.expectEqualStrings("more", lines.at(lines.len - 1));
}

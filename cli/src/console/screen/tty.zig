//! Writing to the terminal: the retrying raw write every frame goes through, cursor
//! placement, and splitting arriving output into scrollback lines.
const std = @import("std");
const logo = @import("../../app/logo.zig");

/// libc write to a raw fd (std.posix.write is unavailable here the same way line_edit uses).
pub fn ttyWrite(fd: std.posix.fd_t, bytes: []const u8) void {
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

/// Append `bytes` to `pending`, flushing a completed owned line into `dst` on each '\n'
/// (the trailing '\r' of a CRLF is dropped). Allocation failures silently drop the line.
pub fn pushChunk(gpa: std.mem.Allocator, dst: *std.ArrayList([]u8), pending: *std.ArrayList(u8), bytes: []const u8) void {
    for (bytes) |ch| {
        if (ch == '\n') {
            var line = pending.items;
            if (line.len != 0 and line[line.len - 1] == '\r') line = line[0 .. line.len - 1];
            const owned = gpa.dupe(u8, line) catch {
                pending.clearRetainingCapacity();
                continue;
            };
            dst.append(gpa, owned) catch gpa.free(owned);
            pending.clearRetainingCapacity();
        } else {
            pending.append(gpa, ch) catch {};
        }
    }
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

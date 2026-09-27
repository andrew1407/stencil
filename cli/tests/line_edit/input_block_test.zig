//! The line editor's full-screen input block, driven over pipes: submitting a wrapped line clears
//! every row it owned, and a refresh keeps the selection wash painted on the input rows.
const std = @import("std");
const le = @import("../../src/line_edit/line_edit.zig");
const screen_mod = @import("../../src/console/screen.zig");
const Editor = le.Editor;
const testing = std.testing;

/// Read a non-blocking fd until it runs dry — the editor emits its escapes in many small
/// writes, so one read() would only ever see the first of them.
fn drain(fd: std.posix.fd_t, sink: []u8) []const u8 {
    var n: usize = 0;
    while (n < sink.len) {
        const got = std.c.read(fd, sink[n..].ptr, sink.len - n);
        if (got <= 0) break;
        n += @intCast(got);
    }
    return sink[0..n];
}

test "submitting a wrapped line clears every row it owned, not just the first" {
    // The bug: endPromptLine erased only promptRow(), so the continuation rows of a wrapped
    // prompt stayed on screen for the whole command — nothing else repaints that band.
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();

    // Non-blocking, so drain() can read until the pipe is empty rather than hanging on it.
    const out = try std.Io.Threaded.pipe2(.{ .NONBLOCK = true });
    defer _ = std.c.close(out[0]);
    defer _ = std.c.close(out[1]);
    var scr = screen_mod.Screen{ .gpa = a, .io = threaded.io(), .fd = out[1], .rows = 20, .cols = 40 };
    defer scr.freeAllForTest();
    var ed = Editor{ .fd_in = out[0], .fd_out = out[1], .orig = std.mem.zeroes(std.posix.termios), .screen = &scr };

    // A line long enough to need three rows at 40 columns with a 2-column prompt.
    var long: [100]u8 = undefined;
    @memset(&long, 'x');
    ed.refresh("> ", &long, long.len);
    try testing.expectEqual(@as(u16, 3), scr.promptRows());

    // Drain what the refresh wrote, then submit: every row of the block must be erased…
    var sink: [65536]u8 = undefined;
    _ = drain(out[0], &sink);
    ed.endPromptLine();
    const written = drain(out[0], &sink);
    // Rows 18, 19 and 20 are the block on a 20-row screen: each is addressed and erased.
    try testing.expect(std.mem.indexOf(u8, written, "\x1b[18;1H\x1b[2K") != null);
    try testing.expect(std.mem.indexOf(u8, written, "\x1b[19;1H\x1b[2K") != null);
    try testing.expect(std.mem.indexOf(u8, written, "\x1b[20;1H\x1b[2K") != null);
    // …and the block shrinks back to one row, giving the output area its rows back.
    try testing.expectEqual(@as(u16, 1), scr.promptRows());
}

test "refresh keeps a selection wash on the input rows instead of erasing it" {
    // The bug: every mouse event ends in refresh(), which repaints the input rows — wiping the
    // highlight the screen had just drawn there, so a drag over the typed line showed nothing.
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();

    const out = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(out[0]);
    defer _ = std.c.close(out[1]);
    var scr = screen_mod.Screen{ .gpa = a, .io = threaded.io(), .fd = out[1], .rows = 10, .cols = 40 };
    defer scr.freeAllForTest();

    var ed = Editor{ .fd_in = out[0], .fd_out = out[1], .orig = std.mem.zeroes(std.posix.termios), .screen = &scr };
    const line = "hello world";

    // No selection: the row is painted plainly.
    ed.refresh("> ", line, line.len);
    try testing.expect(!scr.hasHighlight());

    // With one covering the input row, a refresh must still leave the wash on screen.
    scr.selectForTest(scr.promptRow(), 3, scr.promptRow(), 8);
    var drained: [8192]u8 = undefined;
    _ = std.posix.read(out[0], &drained) catch 0; // ignore what came before
    ed.refresh("> ", line, line.len);
    const n = std.posix.read(out[0], &drained) catch 0;
    const painted = drained[0..n];
    try testing.expect(std.mem.indexOf(u8, painted, "\x1b[48;2;") != null); // the wash survived
}

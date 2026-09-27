//! What the terminal says is not what the user types: a late colour answer (OSC 17), a DA1 or
//! mode report never lands in the line, a lone ESC does not sit waiting for a second key, and
//! keys typed while a background call is watched are kept for the prompt.
const std = @import("std");
const le = @import("../../src/line_edit/line_edit.zig");
const testing = std.testing;

fn readOne(keys: []const u8) ![]const u8 {
    const S = struct {
        var buf: [le.max_line]u8 = undefined;
    };
    const in = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(in[0]);
    const out = try std.Io.Threaded.pipe2(.{ .NONBLOCK = true });
    defer _ = std.c.close(out[0]);
    defer _ = std.c.close(out[1]);
    var ed = le.Editor{ .fd_in = in[0], .fd_out = out[1], .orig = std.mem.zeroes(std.posix.termios) };
    var hist = le.History{ .gpa = testing.allocator };
    defer hist.deinit();
    var armed = false;
    _ = std.c.write(in[1], keys.ptr, keys.len);
    _ = std.c.close(in[1]);
    const res = ed.readLine("> ", &S.buf, &hist, &.{}, &armed, "");
    return S.buf[0..res.line];
}

test "terminal replies: a late OSC 17 answer and a DA1 report are swallowed, not typed" {
    try testing.expectEqualStrings("abcd", try readOne("ab\x1b]17;rgb:1111/2222/3333\x07c\x1b[?62;22cd\r"));
    try testing.expectEqualStrings("xy", try readOne("x\x1b]17;rgb:1/2/3\x1b\\y\r")); // ST-terminated
    try testing.expectEqualStrings("ok", try readOne("o\x1b[?2026;2$yk\r")); // a mode report
}

test "a lone ESC gives up on its sequence at once and typing carries on" {
    const in = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(in[0]);
    const out = try std.Io.Threaded.pipe2(.{ .NONBLOCK = true });
    defer _ = std.c.close(out[0]);
    defer _ = std.c.close(out[1]);
    var ed = le.Editor{ .fd_in = in[0], .fd_out = out[1], .orig = std.mem.zeroes(std.posix.termios) };
    var hist = le.History{ .gpa = testing.allocator };
    defer hist.deinit();
    var armed = false;
    var buf: [le.max_line]u8 = undefined;
    _ = std.c.write(in[1], "ab\x1b", 3);
    const Later = struct {
        fn type_(fd: std.posix.fd_t) void {
            var ts: std.c.timespec = .{ .sec = 0, .nsec = 200 * std.time.ns_per_ms };
            _ = std.c.nanosleep(&ts, null);
            _ = std.c.write(fd, "c\r", 2);
        }
    };
    const t = try std.Thread.spawn(.{}, Later.type_, .{in[1]});
    const res = ed.readLine("> ", &buf, &hist, &.{}, &armed, "");
    t.join();
    _ = std.c.close(in[1]);
    // Had the ESC waited for its next byte, the `c` would have been read as Alt-c and lost.
    try testing.expectEqualStrings("abc", buf[0..res.line]);
}

test "pollKeep: a Ctrl-C ends the watch, and what was typed meanwhile reaches the prompt" {
    const in = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(in[0]);
    const out = try std.Io.Threaded.pipe2(.{ .NONBLOCK = true });
    defer _ = std.c.close(out[0]);
    defer _ = std.c.close(out[1]);
    var ed = le.Editor{ .fd_in = in[0], .fd_out = out[1], .orig = std.mem.zeroes(std.posix.termios) };
    var hist = le.History{ .gpa = testing.allocator };
    defer hist.deinit();
    var armed = false;
    var buf: [le.max_line]u8 = undefined;
    _ = std.c.write(in[1], "ab", 2);
    try testing.expect(!ed.pollKeep(0)); // typing is not a cancel …
    _ = std.c.write(in[1], "c\x03d", 3);
    try testing.expect(ed.pollKeep(0)); // … a Ctrl-C is
    _ = std.c.write(in[1], "\r", 1);
    _ = std.c.close(in[1]);
    const res = ed.readLine("> ", &buf, &hist, &.{}, &armed, "");
    try testing.expectEqualStrings("abcd", buf[0..res.line]); // nothing typed during the call was lost
}

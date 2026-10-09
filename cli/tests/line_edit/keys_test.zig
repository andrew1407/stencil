//! The line editor's keys, driven over pipes: the word-delete chords, the erase byte, Ctrl-C's
//! copy-or-exit, the cancel watch, and the paste / un-paste keys.
//! No tty: `readLine` only reads bytes.
const std = @import("std");
const le = @import("../../src/line_edit/line_edit.zig");
const Pipes = @import("pipes.zig").Pipes;
const screen_mod = @import("../../src/console/screen.zig");

const Editor = le.Editor;
const History = le.History;
const max_line = le.max_line;
const testing = std.testing;

test "word-delete chords: every encoding a terminal sends for a modified Backspace" {
    // 0x08 (Ctrl-Backspace on VS Code/Windows/Linux), ESC DEL (Alt-Backspace), the CSI-u modified form
    // and plain Ctrl-W all kill the word; plain Backspace and a BARE CSI-u still take one character.
    const cases = [_]struct { keys: []const u8, want: []const u8 }{
        .{ .keys = "\x7f", .want = "/crop one two thre" },
        .{ .keys = "\x08", .want = "/crop one two " },
        .{ .keys = "\x1b\x7f", .want = "/crop one two " },
        .{ .keys = "\x1b[127;5u", .want = "/crop one two " },
        .{ .keys = "\x1b[127u", .want = "/crop one two thre" },
        .{ .keys = "\x17", .want = "/crop one two " },
    };
    for (cases) |c| {
        const pipes = try Pipes.open();
        defer pipes.close();
        const in = pipes.in;
        const out = pipes.out;
        var ed = Editor{ .fd_in = in[0], .fd_out = out[1], .orig = std.mem.zeroes(std.posix.termios) };
        var hist = History{ .gpa = testing.allocator };
        defer hist.deinit();
        var buf: [max_line]u8 = undefined;
        var armed = false;

        const typed = "/crop one two three";
        _ = std.c.write(in[1], typed.ptr, typed.len);
        _ = std.c.write(in[1], c.keys.ptr, c.keys.len);
        _ = std.c.write(in[1], "\r", 1);
        _ = std.c.close(in[1]);
        const res = ed.readLine("> ", &buf, &hist, &.{}, &armed, "");
        try testing.expectEqualStrings(c.want, buf[0..res.line]);
    }
}

test "a tty that erases with 0x08 keeps it as a plain backspace" {
    // The one terminal family where 0x08 IS the erase key: it must not eat a whole word there.
    const pipes = try Pipes.open();
    defer pipes.close();
    const in = pipes.in;
    const out = pipes.out;
    var ed = Editor{ .fd_in = in[0], .fd_out = out[1], .orig = std.mem.zeroes(std.posix.termios), .erase = 8 };
    var hist = History{ .gpa = testing.allocator };
    defer hist.deinit();
    var buf: [max_line]u8 = undefined;
    var armed = false;

    const keys = "/crop one two three\x08\r";
    _ = std.c.write(in[1], keys.ptr, keys.len);
    _ = std.c.close(in[1]);
    const res = ed.readLine("> ", &buf, &hist, &.{}, &armed, "");
    try testing.expectEqualStrings("/crop one two thre", buf[0..res.line]);
}

test "Ctrl-C copies a live selection; with none it still confirms the exit" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const pipes = try Pipes.open();
    defer pipes.close();
    const in = pipes.in;
    const out = pipes.out;

    var scr = screen_mod.Screen{ .gpa = a, .io = threaded.io(), .fd = out[1], .rows = 20, .cols = 40 };
    defer scr.freeAllForTest();
    const Copied = struct {
        var count: usize = 0;
        fn sink(_: *anyopaque, _: []const u8) void {
            count += 1;
        }
    };
    Copied.count = 0;
    var ctx: u8 = 0;
    var ed = Editor{ .fd_in = in[0], .fd_out = out[1], .orig = std.mem.zeroes(std.posix.termios), .screen = &scr };
    ed.copy_text_cb = Copied.sink;
    ed.logo_ctx = &ctx;
    var hist = History{ .gpa = testing.allocator };
    defer hist.deinit();
    var buf: [max_line]u8 = undefined;
    var armed = false;

    ed.refresh("> ", "hello world", 11); // the prompt row has to exist before a drag can cover it
    scr.selectForTest(scr.promptRow(), 3, scr.promptRow(), 7); // mouse columns are 1-based
    const keys = [_]u8{ 3, 3 }; // the first press copies, the second (no selection left) exits
    _ = std.c.write(in[1], &keys, keys.len);
    _ = std.c.close(in[1]);
    try testing.expect(ed.readLine("> ", &buf, &hist, &.{}, &armed, "") == .interrupt);
    try testing.expectEqual(@as(usize, 1), Copied.count); // copied once, exited once
}

test "pollInterrupt: a Ctrl-C is reported, other type-ahead is dropped" {
    const pipes = try Pipes.open();
    defer pipes.close();
    const in = pipes.in;
    const out = pipes.out;
    var ed = Editor{ .fd_in = in[0], .fd_out = out[1], .orig = std.mem.zeroes(std.posix.termios) };

    const typed = [_]u8{ 'a', 'b', '\n' }; // keystrokes during a call have no line to land in
    _ = std.c.write(in[1], &typed, typed.len);
    try testing.expect(!ed.pollInterrupt(0));

    const press = [_]u8{ 'x', 3 }; // …and a Ctrl-C among them still reads as "cancel this"
    _ = std.c.write(in[1], &press, press.len);
    try testing.expect(ed.pollInterrupt(0));

    // Everything was consumed: nothing is left to arm a quit once the call returns.
    try testing.expect(!ed.pollInterrupt(0));
    _ = std.c.close(in[1]);
}

test "plain Ctrl-V / Ctrl-Z resolve to the paste / un-paste actions" {
    // Driven over a pipe rather than a tty: readLine only reads bytes, and the actions under test need
    // no screen. Ctrl-V is delivered untouched by terminals, unlike the Option/Meta chords.
    const pipes = try Pipes.open();
    defer pipes.close();
    const in = pipes.in;
    const out = pipes.out;

    var ed = Editor{ .fd_in = in[0], .fd_out = out[1], .orig = std.mem.zeroes(std.posix.termios) };
    var hist = History{ .gpa = testing.allocator };
    defer hist.deinit();
    var buf: [max_line]u8 = undefined;
    var armed = false;

    const keys = [_]u8{ 22, 26 }; // Ctrl-V then Ctrl-Z
    _ = std.c.write(in[1], &keys, keys.len); // libc write, like refresh() (no std.posix.write)
    _ = std.c.close(in[1]);

    try testing.expect(ed.readLine("> ", &buf, &hist, &.{}, &armed, "") == .paste);
    try testing.expect(ed.readLine("> ", &buf, &hist, &.{}, &armed, "") == .unpaste);
    // The stream ends there: a closed input still leaves the console, as before.
    try testing.expect(ed.readLine("> ", &buf, &hist, &.{}, &armed, "") == .eof);
}

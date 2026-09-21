//! The console's "still working" line for a slow call (the /prompt turn): one line whose
//! leading glyph circles through `frames` while the request runs, erased the moment the
//! reply — or any other output — lands. The bot's ProgressNotice, in terminal form.
const std = @import("std");
const logo = @import("../../app/logo.zig");
const screen = @import("../screen.zig");

/// The spinner frames, in order — a filled quarter circling clockwise (the bot's set).
pub const frames = [_][]const u8{ "◐", "◓", "◑", "◒" };

/// How long one frame holds — a full turn every 600 ms, advanced on the transport's wait beat.
pub const frame_ms: i64 = 150;

const max_line = 256;

/// The line at frame `i` — the glyph in the theme accent, then the text. accentSeq() (not
/// accentReal()) so the stored line carries the sentinel and re-tints with a `/theme` change.
pub fn frameLine(buf: []u8, i: usize, text: []const u8) []const u8 {
    return std.fmt.bufPrint(buf, "{s}{s}{s} {s}", .{ logo.accentSeq(), frames[i % frames.len], logo.resetSeq(), text }) catch buf[0..0];
}

pub const Spinner = struct {
    // `still`: no terminal to redraw on (piped input, a test sink), so the line is printed once.
    // `screen`: it lives in the full-screen scrollback and is swapped in place. `tty`: rewritten with CR.
    const Mode = enum { still, screen, tty };

    text: []const u8 = "",
    mode: Mode = .still,
    frame: usize = 0,
    due_ms: ?i64 = null, // when the frame next advances (null = the first beat sets it)
    line: usize = 0, // scrollback index of the line (screen mode)
    shown: bool = false,

    /// Print the line and, on a live terminal, arm it to spin. `self` must keep its address
    /// until `stop`: the pre-print hook points at it.
    pub fn start(self: *Spinner, text: []const u8) void {
        self.* = .{ .text = text };
        var buf: [max_line]u8 = undefined;
        const first = frameLine(&buf, 0, text);
        if (screen.current()) |scr| {
            logo.print("{s}\n", .{first});
            self.mode = .screen;
            self.line = scr.lines.items.len -| 1;
        } else if (logo.liveTty()) {
            std.debug.print("{s}", .{first}); // no newline: the row is redrawn in place
            self.mode = .tty;
        } else {
            logo.print("{s}\n", .{first});
            return;
        }
        self.shown = true;
        // Whatever prints while it spins erases it first, so the line never sits above — or,
        // on a plain tty, in front of — the output that ends the wait.
        logo.armPrePrint(erasePrePrint, self);
    }

    /// One wait beat at clock `now_ms`: advance the frame once `frame_ms` has passed.
    pub fn beat(self: *Spinner, now_ms: i64) void {
        if (!self.shown) return;
        const due = self.due_ms orelse {
            self.due_ms = now_ms + frame_ms;
            return;
        };
        if (now_ms < due) return;
        self.due_ms = now_ms + frame_ms;
        const prev = self.frame;
        self.frame = (self.frame + 1) % frames.len;
        var was: [max_line]u8 = undefined;
        var now: [max_line]u8 = undefined;
        switch (self.mode) {
            .screen => {
                const scr = screen.current() orelse return;
                // Output since then moved the line: stop touching the scrollback.
                if (!scr.replaceLine(self.line, frameLine(&was, prev, self.text), frameLine(&now, self.frame, self.text)))
                    self.shown = false;
            },
            .tty => std.debug.print("\r{s}", .{frameLine(&now, self.frame, self.text)}),
            .still => {},
        }
    }

    /// The wait is over: erase the line and let go of the hook. Idempotent.
    pub fn stop(self: *Spinner) void {
        if (self.mode == .still) return;
        logo.disarmPrePrint();
        self.erase();
        self.mode = .still;
    }

    fn erase(self: *Spinner) void {
        if (!self.shown) return;
        self.shown = false;
        var buf: [max_line]u8 = undefined;
        switch (self.mode) {
            .screen => if (screen.current()) |scr| {
                _ = scr.removeLine(self.line, frameLine(&buf, self.frame, self.text));
            },
            .tty => std.debug.print("\r\x1b[K", .{}),
            .still => {},
        }
    }

    fn erasePrePrint(raw: *anyopaque) void {
        const self: *Spinner = @ptrCast(@alignCast(raw));
        self.erase();
    }
};

const testing = std.testing;

/// A logo.print sink collecting what the console told the user.
const Sink = struct {
    buf: std.ArrayList(u8) = .empty,
    fn take(ctx: *anyopaque, chunk: []const u8) void {
        const self: *Sink = @ptrCast(@alignCast(ctx));
        self.buf.appendSlice(testing.allocator, chunk) catch {};
    }
};

/// `frameLine` as the tests expect it: the glyph wrapped in the live accent, then the text.
fn expectLine(buf: []u8, glyph: []const u8, text: []const u8) []const u8 {
    return std.fmt.bufPrint(buf, "{s}{s}{s} {s}", .{ logo.accentSeq(), glyph, logo.resetSeq(), text }) catch unreachable;
}

test "frameLine: the glyph circles clockwise and wraps, painted in the theme accent" {
    var buf: [64]u8 = undefined;
    var want: [64]u8 = undefined;
    try testing.expectEqualStrings(expectLine(&want, "◐", "x"), frameLine(&buf, 0, "x"));
    try testing.expectEqualStrings(expectLine(&want, "◓", "x"), frameLine(&buf, 1, "x"));
    try testing.expectEqualStrings(expectLine(&want, "◑", "x"), frameLine(&buf, 2, "x"));
    try testing.expectEqualStrings(expectLine(&want, "◒", "x"), frameLine(&buf, 3, "x"));
    try testing.expectEqualStrings(expectLine(&want, "◐", "x"), frameLine(&buf, 4, "x"));
    // Only the glyph is coloured — the text after it is plain.
    logo.init(false, false);
    try testing.expect(std.mem.startsWith(u8, frameLine(&buf, 0, "x"), logo.accentSeq()));
    try testing.expect(std.mem.endsWith(u8, frameLine(&buf, 0, "x"), " x"));
    try testing.expect(std.mem.indexOf(u8, frameLine(&buf, 0, "x"), "\x1b[0m x") != null);
    // With colour off there is no escape at all.
    logo.init(true, false);
    defer logo.init(false, false);
    try testing.expectEqualStrings("◐ x", frameLine(&buf, 0, "x"));
}

test "off a terminal the line is printed once and stays still" {
    var sink = Sink{};
    defer sink.buf.deinit(testing.allocator);
    logo.setSink(Sink.take, &sink);
    defer logo.clearSink();
    var spin = Spinner{};
    spin.start("thinking…");
    var want: [64]u8 = undefined;
    const first = expectLine(&want, "◐", "thinking…");
    try testing.expectEqual(sink.buf.items.len, first.len + 1);
    try testing.expectEqualStrings(first, sink.buf.items[0..first.len]);
    spin.beat(0);
    spin.beat(10_000);
    spin.stop();
    logo.print("done\n", .{});
    try testing.expectEqualStrings("\ndone\n", sink.buf.items[first.len..]);
}

test "in the full-screen console the line spins in place and goes with the first output" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var s = screen.Screen{ .gpa = a, .io = threaded.io(), .fd = -1, .rows = 10, .cols = 60 };
    defer s.freeAllForTest();
    s.setRevealSpeed(1.0);
    s.installForTest();
    defer s.uninstallForTest();
    s.append("wrote out.png\n");

    var want: [64]u8 = undefined;
    var spin = Spinner{};
    spin.start("thinking…");
    try testing.expectEqual(@as(usize, 2), s.lines.items.len);
    try testing.expectEqualStrings(expectLine(&want, "◐", "thinking…"), s.lines.items[1]);
    // The first beat only starts the clock; the frame advances once frame_ms has passed.
    spin.beat(1000);
    try testing.expectEqualStrings(expectLine(&want, "◐", "thinking…"), s.lines.items[1]);
    spin.beat(1000 + frame_ms - 1);
    try testing.expectEqualStrings(expectLine(&want, "◐", "thinking…"), s.lines.items[1]);
    spin.beat(1000 + frame_ms);
    try testing.expectEqualStrings(expectLine(&want, "◓", "thinking…"), s.lines.items[1]);
    spin.beat(1000 + 2 * frame_ms);
    try testing.expectEqualStrings(expectLine(&want, "◑", "thinking…"), s.lines.items[1]);
    // The reply lands: its print erases the line first, so the scrollback keeps no trace.
    logo.print("reply: done\n", .{});
    try testing.expectEqual(@as(usize, 2), s.lines.items.len);
    try testing.expectEqualStrings("wrote out.png", s.lines.items[0]);
    try testing.expectEqualStrings("reply: done", s.lines.items[1]);
    spin.beat(1000 + 3 * frame_ms); // inert now: nothing to advance
    spin.stop();
    try testing.expectEqual(@as(usize, 2), s.lines.items.len);
    try testing.expectEqualStrings("reply: done", s.lines.items[1]);
}

test "in the full-screen console a wait with no output still ends with the line erased" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var s = screen.Screen{ .gpa = a, .io = threaded.io(), .fd = -1, .rows = 10, .cols = 60 };
    defer s.freeAllForTest();
    s.setRevealSpeed(1.0);
    s.installForTest();
    defer s.uninstallForTest();

    var want: [64]u8 = undefined;
    var spin = Spinner{};
    spin.start("thinking…");
    spin.beat(0);
    spin.beat(frame_ms);
    try testing.expectEqualStrings(expectLine(&want, "◓", "thinking…"), s.lines.items[0]);
    spin.stop();
    try testing.expectEqual(@as(usize, 0), s.lines.items.len);
    spin.stop(); // idempotent
    logo.print("later\n", .{}); // the hook is gone: nothing else is erased
    try testing.expectEqual(@as(usize, 1), s.lines.items.len);
}

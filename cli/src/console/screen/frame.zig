//! The screen's frame: everything one paint writes, buffered and sent to the terminal as ONE
//! write inside synchronized output (DECSET 2026) with the cursor hidden, so no frame tears.
//! Terminals without 2026 ignore the private mode; an animation flushes one frame per tick.
const std = @import("std");
const tty = @import("tty.zig");
const Screen = @import("../screen.zig").Screen;

pub const open_seq = "\x1b[?2026h\x1b[?25l";
pub const close_seq = "\x1b[?25h\x1b[?2026l";

pub const Frame = struct {
    fd: std.posix.fd_t = -1,
    gpa: ?std.mem.Allocator = null,
    buf: std.ArrayList(u8) = .empty, // open_seq, then the frame's bytes (empty = nothing pending)
    depth: u16 = 0,
    sent: u32 = 0, // frames flushed so far: a bracket spanning one has painted
    keeping: u16 = 0, // cursor brackets open: each frame sent meanwhile shows the cursor at the prompt

    fn put(self: *Frame, bytes: []const u8) void {
        const gpa = self.gpa orelse return tty.rawWrite(self.fd, bytes);
        if (self.buf.items.len == 0) {
            self.buf.appendSlice(gpa, open_seq) catch return tty.rawWrite(self.fd, bytes);
            if (self.keeping != 0) self.buf.appendSlice(gpa, "\x1b7") catch {};
        }
        self.buf.appendSlice(gpa, bytes) catch {
            self.flush(); // out of memory: what is buffered goes now, this piece straight after
            tty.rawWrite(self.fd, bytes);
        };
    }

    /// Send what is buffered as one write, closed; nothing when the frame drew nothing.
    pub fn flush(self: *Frame) void {
        if (self.buf.items.len == 0) return;
        const gpa = self.gpa orelse return;
        if (self.keeping != 0) self.buf.appendSlice(gpa, "\x1b8") catch {};
        self.buf.appendSlice(gpa, close_seq) catch {
            tty.rawWrite(self.fd, self.buf.items);
            tty.rawWrite(self.fd, close_seq);
            self.buf.clearRetainingCapacity();
            return;
        };
        tty.rawWrite(self.fd, self.buf.items);
        self.buf.clearRetainingCapacity();
        self.sent +%= 1;
    }

    pub fn deinit(self: *Frame) void {
        if (self.gpa) |gpa| self.buf.deinit(gpa);
        self.buf = .empty;
    }
};

// The frame being drawn, if any: `tty.ttyWrite` to its fd lands in it. One screen at a time.
var active: ?*Frame = null;

/// Open a frame on `s` (nested opens join the outer one). A screen with no terminal never buffers.
pub fn begin(s: *Screen) void {
    if (s.fd < 0) return;
    const f = &s.frame;
    if (f.depth == 0) {
        f.fd = s.fd;
        f.gpa = s.gpa;
        active = f;
    }
    f.depth += 1;
}

/// Close it; the outermost close sends the frame.
pub fn end(s: *Screen) void {
    const f = &s.frame;
    if (s.fd < 0 or f.depth == 0) return;
    f.depth -= 1;
    if (f.depth != 0) return;
    f.flush();
    if (active == f) active = null;
}

/// The frame boundary of an animation: what the open frame holds goes out before the wait.
pub fn flushActive() void {
    if (active) |f| f.flush();
}

/// A paint the line editor will not redraw after it — a skin's frame, an effect — puts the cursor
/// back where it found it (DECSC … DECRC), in every frame an animation sends meanwhile. Only the
/// outermost bracket writes; one around nothing takes itself back, so it sends no frame.
pub const CursorKeep = struct { at: usize, after: usize, sent: u32, outer: bool };

pub fn keepCursor(s: *Screen) CursorKeep {
    const f = &s.frame;
    if (f.keeping != 0) {
        f.keeping += 1;
        return .{ .at = 0, .after = 0, .sent = f.sent, .outer = false };
    }
    const at = f.buf.items.len;
    tty.ttyWrite(s.fd, "\x1b7");
    f.keeping = 1;
    return .{ .at = at, .after = f.buf.items.len, .sent = f.sent, .outer = true };
}

pub fn returnCursor(s: *Screen, k: CursorKeep) void {
    const f = &s.frame;
    f.keeping -|= 1;
    if (!k.outer) return;
    if (f.sent == k.sent and f.buf.items.len == k.after and k.after > k.at) {
        f.buf.shrinkRetainingCapacity(k.at);
        return;
    }
    if (f.sent != k.sent and f.buf.items.len == 0) return; // the last frame sent took it home
    tty.ttyWrite(s.fd, "\x1b8");
}

/// Buffer `bytes` when a frame is open on `fd`; false = write them straight out.
pub fn capture(fd: std.posix.fd_t, bytes: []const u8) bool {
    const f = active orelse return false;
    if (f.fd != fd) return false;
    f.put(bytes);
    return true;
}

const testing = std.testing;

const Taps = struct {
    writes: usize = 0,
    bytes: std.ArrayList(u8) = .empty,
    fn take(ctx: *anyopaque, b: []const u8) void {
        const self: *Taps = @ptrCast(@alignCast(ctx));
        self.writes += 1;
        self.bytes.appendSlice(testing.allocator, b) catch {};
    }
};

test "a frame goes out as one write, wrapped; nested frames join, an empty one writes nothing" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    var s = Screen{ .gpa = testing.allocator, .io = threaded.io(), .fd = 991, .rows = 10, .cols = 40 };
    defer s.freeAll();
    var taps = Taps{};
    defer taps.bytes.deinit(testing.allocator);
    tty.tap = .{ .fd = 991, .ctx = &taps, .write = Taps.take };
    defer tty.tap = null;

    begin(&s);
    end(&s);
    try testing.expectEqual(@as(usize, 0), taps.writes);
    begin(&s);
    tty.ttyWrite(991, "a");
    begin(&s);
    tty.ttyWrite(991, "b");
    end(&s);
    try testing.expectEqual(@as(usize, 0), taps.writes); // the inner close sends nothing
    tty.ttyWrite(991, "c");
    end(&s);
    try testing.expectEqual(@as(usize, 1), taps.writes);
    try testing.expectEqualStrings(open_seq ++ "abc" ++ close_seq, taps.bytes.items);
    tty.ttyWrite(991, "d"); // no frame open: straight out
    try testing.expectEqual(@as(usize, 2), taps.writes);
}

test "an animation's wait sends the frame so far, and the next one starts fresh" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    var s = Screen{ .gpa = testing.allocator, .io = threaded.io(), .fd = 992, .rows = 10, .cols = 40 };
    defer s.freeAll();
    var taps = Taps{};
    defer taps.bytes.deinit(testing.allocator);
    tty.tap = .{ .fd = 992, .ctx = &taps, .write = Taps.take };
    defer tty.tap = null;

    begin(&s);
    tty.ttyWrite(992, "one");
    flushActive();
    tty.ttyWrite(992, "two");
    end(&s);
    try testing.expectEqual(@as(usize, 2), taps.writes);
    try testing.expectEqualStrings(open_seq ++ "one" ++ close_seq ++ open_seq ++ "two" ++ close_seq, taps.bytes.items);
}

test "a cursor bracket: around nothing no frame, around each frame of an animation, nested once" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    var s = Screen{ .gpa = testing.allocator, .io = threaded.io(), .fd = 993, .rows = 10, .cols = 40 };
    defer s.freeAll();
    var taps = Taps{};
    defer taps.bytes.deinit(testing.allocator);
    tty.tap = .{ .fd = 993, .ctx = &taps, .write = Taps.take };
    defer tty.tap = null;
    const home = "\x1b7";
    const back = "\x1b8";

    begin(&s);
    returnCursor(&s, keepCursor(&s));
    end(&s);
    try testing.expectEqual(@as(usize, 0), taps.writes);
    begin(&s);
    const k = keepCursor(&s);
    tty.ttyWrite(993, "one");
    flushActive();
    tty.ttyWrite(993, "two");
    flushActive();
    returnCursor(&s, k);
    end(&s);
    try testing.expectEqual(@as(usize, 2), taps.writes);
    try testing.expectEqualStrings(open_seq ++ home ++ "one" ++ back ++ close_seq ++
        open_seq ++ home ++ "two" ++ back ++ close_seq, taps.bytes.items);
    taps.bytes.clearRetainingCapacity();
    begin(&s);
    const outer = keepCursor(&s);
    const inner = keepCursor(&s); // an effect inside a skin frame: the outer bracket speaks for both
    tty.ttyWrite(993, "x");
    returnCursor(&s, inner);
    returnCursor(&s, outer);
    end(&s);
    try testing.expectEqualStrings(open_seq ++ home ++ "x" ++ back ++ close_seq, taps.bytes.items);
}

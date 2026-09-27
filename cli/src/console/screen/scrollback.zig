//! The scrollback buffer: appending output (wrapping it to the terminal width), replacing or
//! removing the last line, and freeing it all. logo's print sink feeds this.
const std = @import("std");
const sc = @import("../screen.zig");
const Screen = sc.Screen;
const ansi = @import("../render/ansi.zig");
const logoFx = @import("../render/logoFx.zig");
const sanitize = @import("../../safety/sanitize.zig");
const frame = @import("frame.zig");
const diff = @import("diff.zig");
const tty = @import("tty.zig");
const pushChunk = sc.pushChunk;
const max_lines = sc.max_lines;
const trim_batch = 512; // lines past the cap that wait for the next batch trim

/// Feed captured output into the scrollback (one logical line per '\n'), then repaint. Plain output
/// paints only what changed; a reveal sweep or a secret skin paints the whole body as it always has.
/// A partial line changes nothing on screen, so it paints nothing under any look.
pub fn append(self: *Screen, bytes: []const u8) void {
    frame.begin(self);
    defer frame.end(self);
    const had_sel = self.has_sel;
    self.has_sel = false; // new output invalidates any highlight
    self.sel_active = false;
    self.drag_owed = false;
    const before = self.lines.len;
    const first_shown = self.dropped + self.window().first;
    pushChunk(self.gpa, &self.lines, &self.pending, bytes);
    cleanNewLines(self, before);
    self.wrapNewLines(before);
    const grew = self.lines.len -| before;
    trimOldest(self);
    if (self.scroll_off != 0) {
        // Scrolled up: keep the viewport anchored on what the user is reading.
        self.scroll_off += grew;
        self.clampScroll();
        if (had_sel and diff.plain()) return diff.paintChanged(self); // only the rows it lit
        if (had_sel) self.paintBody();
        if (grew != 0 or had_sel) self.drawStatusBar(); // a partial line changes nothing on screen
        return;
    }
    const skip = self.skip_reveal_once;
    self.skip_reveal_once = false; // one-shot, consumed whether it applied or not
    if (grew == 0 and !had_sel) return;
    if (grew != 0 and !skip and logoFx.revealing(self)) {
        // The lines just added sweep in from the left; everything else lands at once.
        logoFx.revealNew(self, @min(grew, self.lines.len));
        self.drawStatusBar();
    } else if (!diff.plain()) {
        self.paintBody();
        self.drawStatusBar();
    } else {
        const moved = self.dropped + self.window().first - first_shown;
        if (!had_sel and moved != 0) diff.shiftBody(self, @intCast(moved));
        diff.paintChanged(self);
    }
}

// A line of output is text and colour: a carriage return starts it over (only what follows the
// last one is kept) and every other control sequence is dropped — see sanitize.stripControls.
fn cleanNewLines(self: *Screen, from: usize) void {
    for (from..self.lines.len) |i| {
        const line = self.lines.atPtr(i);
        const cr = if (std.mem.lastIndexOfScalar(u8, line.*, '\r')) |at| at + 1 else 0;
        const kept = sanitize.stripControls(line.*, line.*[cr..], .sgr);
        if (kept.len == line.len) continue;
        const copy = self.gpa.dupe(u8, kept) catch continue;
        self.gpa.free(line.*);
        line.* = copy;
    }
}

// Past the cap the oldest lines go in one batch, off the ring's front, so nothing else moves;
// `dropped` counts them, keeping on-screen positions comparable across a trim.
fn trimOldest(self: *Screen) void {
    if (self.lines.len <= max_lines + trim_batch) return;
    const n = self.lines.len - max_lines;
    for (0..n) |_| self.gpa.free(self.lines.popFront().?);
    self.dropped += n;
}

/// Re-split lines just added that are wider than the window. One scrollback line stays one screen row
/// (keeping scrolling and selection simple); already-wrapped lines keep their wrap width on resize.
pub fn wrapNewLines(self: *Screen, from: usize) void {
    if (self.cols == 0) return;
    var i = from;
    while (i < self.lines.len) : (i += 1) {
        const line = self.lines.at(i);
        const cut = ansi.splitAt(line, self.cols);
        if (cut >= line.len) continue; // fits
        const head = self.gpa.dupe(u8, line[0..cut]) catch continue;
        const tail = self.gpa.dupe(u8, line[cut..]) catch {
            self.gpa.free(head);
            continue;
        };
        tty.insertAt(&self.lines, self.gpa, i + 1, tail) catch {
            self.gpa.free(head);
            self.gpa.free(tail);
            continue;
        };
        self.lines.atPtr(i).* = head;
        self.gpa.free(line);
        // The tail is re-examined on the next turn of the loop, so a very long line keeps
        // splitting until every piece fits.
    }
}

/// Swap scrollback line `idx` for `bytes` in place — a transient notice advancing a frame. Only
/// while that line still reads `expect`: output since then may have moved or wrapped it.
pub fn replaceLine(self: *Screen, idx: usize, expect: []const u8, bytes: []const u8) bool {
    if (!self.lineIs(idx, expect)) return false;
    const copy = self.gpa.dupe(u8, bytes) catch return false;
    self.gpa.free(self.lines.at(idx));
    self.lines.atPtr(idx).* = copy;
    frame.begin(self);
    defer frame.end(self);
    if (diff.plain()) diff.paintChanged(self) else self.paintBody();
    return true;
}

/// Drop scrollback line `idx` — a transient notice whose wait is over — under the same
/// guard as replaceLine. True when it went.
pub fn removeLine(self: *Screen, idx: usize, expect: []const u8) bool {
    if (!self.lineIs(idx, expect)) return false;
    self.gpa.free(tty.removeAt(&self.lines, idx));
    self.has_sel = false; // the rows below moved up, so a highlight no longer marks its text
    self.sel_active = false;
    self.clampScroll();
    frame.begin(self);
    defer frame.end(self);
    diff.paintChanged(self);
    return true;
}

/// Drop all scrollback (the `/clear` command) and repaint an empty body.
pub fn clearScrollback(self: *Screen) void {
    tty.clearLines(&self.lines, self.gpa);
    self.pending.clearRetainingCapacity();
    self.scroll_off = 0;
    frame.begin(self);
    defer frame.end(self);
    diff.paintChanged(self);
}

pub fn freeAll(self: *Screen) void {
    for (self.prompt_lines.items) |l| self.gpa.free(l);
    self.prompt_lines.deinit(self.gpa);
    for (self.header.items) |l| self.gpa.free(l);
    self.header.deinit(self.gpa);
    tty.clearLines(&self.lines, self.gpa);
    self.lines.deinit(self.gpa);
    self.pending.deinit(self.gpa);
    self.hdr_pending.deinit(self.gpa);
    self.sel_buf.deinit(self.gpa);
    self.frame.deinit();
}

pub fn sinkTrampoline(ctx: *anyopaque, bytes: []const u8) void {
    const self: *Screen = @ptrCast(@alignCast(ctx));
    if (self.alt_capture) |dst|
        pushChunk(self.gpa, dst, &self.hdr_pending, bytes)
    else if (self.capturing_header)
        pushChunk(self.gpa, &self.header, &self.hdr_pending, bytes)
    else
        self.append(bytes);
}

pub fn freeAllForTest(self: *Screen) void {
    self.freeAll();
}

const testing = std.testing;

test "skipRevealOnce: the echo of a typed line lands at once, and only that line" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var s = Screen{ .gpa = a, .io = threaded.io(), .fd = -1, .rows = 10, .cols = 40 };
    defer s.freeAll();
    try testing.expect(!s.skip_reveal_once);
    s.skipRevealOnce(); // console.zig arms this around the command echo
    try testing.expect(s.skip_reveal_once);
    s.append("> /reveal-speed 0.05\n");
    // One-shot: consumed by that append, so the command's real output still sweeps in.
    try testing.expect(!s.skip_reveal_once);
    try testing.expectEqual(@as(usize, 1), s.lines.len);
    // Armed but nothing arrives (an empty line): still consumed, never left standing.
    s.skipRevealOnce();
    s.append("");
    try testing.expect(!s.skip_reveal_once);
}

test "replaceLine / removeLine: only the line that still reads as expected is touched" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var s = Screen{ .gpa = a, .io = threaded.io(), .fd = -1, .rows = 10, .cols = 40 };
    defer s.freeAll();
    s.setRevealSpeed(1.0);
    s.append("one\n◐ wait\nthree\n");
    try testing.expect(s.replaceLine(1, "◐ wait", "◓ wait"));
    try testing.expectEqualStrings("◓ wait", s.lines.at(1));
    // Stale expectation (the frame already moved on) or a bad index: untouched.
    try testing.expect(!s.replaceLine(1, "◐ wait", "◑ wait"));
    try testing.expect(!s.replaceLine(7, "◓ wait", "◑ wait"));
    try testing.expectEqualStrings("◓ wait", s.lines.at(1));
    try testing.expect(!s.removeLine(1, "◐ wait"));
    try testing.expectEqual(@as(usize, 3), s.lines.len);
    try testing.expect(s.removeLine(1, "◓ wait"));
    try testing.expectEqual(@as(usize, 2), s.lines.len);
    try testing.expectEqualStrings("one", s.lines.at(0));
    try testing.expectEqualStrings("three", s.lines.at(1));
}

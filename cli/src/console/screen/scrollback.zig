//! The scrollback buffer: appending output (wrapping it to the terminal width), replacing or
//! removing the last line, and freeing it all. logo's print sink feeds this.
const sc = @import("../screen.zig");
const Screen = sc.Screen;
const ansi = @import("../ansi.zig");
const logoFx = @import("../logoFx.zig");
const pushChunk = sc.pushChunk;
const max_lines = sc.max_lines;

/// Feed captured output into the scrollback (one logical line per '\n'), then repaint.
pub fn append(self: *Screen, bytes: []const u8) void {
    self.has_sel = false; // new output invalidates any highlight
    self.sel_active = false;
    const before = self.lines.items.len;
    pushChunk(self.gpa, &self.lines, &self.pending, bytes);
    self.wrapNewLines(before);
    // Cap the buffer, freeing the oldest lines.
    while (self.lines.items.len > max_lines) {
        self.gpa.free(self.lines.orderedRemove(0));
    }
    if (self.scroll_off == 0) {
        // The lines just added sweep in from the left; everything else lands at once.
        const grew = self.lines.items.len -| before;
        const skip = self.skip_reveal_once;
        self.skip_reveal_once = false; // one-shot, consumed whether it applied or not
        if (grew != 0 and !skip and logoFx.revealing(self))
            logoFx.revealNew(self, @min(grew, self.lines.items.len))
        else
            self.paintBody();
        self.drawStatusBar();
    } else {
        // Scrolled up: keep the viewport anchored on what the user is reading.
        const added = self.lines.items.len - @min(before, self.lines.items.len);
        self.scroll_off += added;
        self.clampScroll();
        self.drawStatusBar();
    }
}

/// Re-split lines just added that are wider than the window. One scrollback line stays one screen row
/// (keeping scrolling and selection simple); already-wrapped lines keep their wrap width on resize.
pub fn wrapNewLines(self: *Screen, from: usize) void {
    if (self.cols == 0) return;
    var i = from;
    while (i < self.lines.items.len) : (i += 1) {
        const cut = ansi.splitAt(self.lines.items[i], self.cols);
        if (cut >= self.lines.items[i].len) continue; // fits
        const line = self.lines.items[i];
        const head = self.gpa.dupe(u8, line[0..cut]) catch continue;
        const tail = self.gpa.dupe(u8, line[cut..]) catch {
            self.gpa.free(head);
            continue;
        };
        self.lines.insert(self.gpa, i + 1, tail) catch {
            self.gpa.free(head);
            self.gpa.free(tail);
            continue;
        };
        self.lines.items[i] = head;
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
    self.gpa.free(self.lines.items[idx]);
    self.lines.items[idx] = copy;
    self.paintBody();
    return true;
}

/// Drop scrollback line `idx` — a transient notice whose wait is over — under the same
/// guard as replaceLine. True when it went.
pub fn removeLine(self: *Screen, idx: usize, expect: []const u8) bool {
    if (!self.lineIs(idx, expect)) return false;
    self.gpa.free(self.lines.orderedRemove(idx));
    self.has_sel = false; // the rows below moved up, so a highlight no longer marks its text
    self.sel_active = false;
    self.clampScroll();
    self.paintBody();
    self.drawStatusBar();
    return true;
}

/// Drop all scrollback (the `/clear` command) and repaint an empty body.
pub fn clearScrollback(self: *Screen) void {
    for (self.lines.items) |l| self.gpa.free(l);
    self.lines.clearRetainingCapacity();
    self.pending.clearRetainingCapacity();
    self.scroll_off = 0;
    self.paintBody();
    self.drawStatusBar();
}

pub fn freeAll(self: *Screen) void {
    for (self.prompt_lines.items) |l| self.gpa.free(l);
    self.prompt_lines.deinit(self.gpa);
    for (self.header.items) |l| self.gpa.free(l);
    self.header.deinit(self.gpa);
    for (self.lines.items) |l| self.gpa.free(l);
    self.lines.deinit(self.gpa);
    self.pending.deinit(self.gpa);
    self.hdr_pending.deinit(self.gpa);
    self.sel_buf.deinit(self.gpa);
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

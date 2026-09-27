//! The viewport's geometry: which scrollback lines the body shows, where the status bar and
//! the prompt sit, and how a scroll moves the window. Pure arithmetic over the Screen's state.
const sc = @import("../screen.zig");
const Screen = sc.Screen;
const std = @import("std");
const Window = sc.Screen.Window;
const wheel_step = sc.wheel_step;
const frame = @import("frame.zig");
const diff = @import("diff.zig");

pub fn headerRows(self: *Screen) u16 {
    return @intCast(self.header.items.len);
}
/// Max rows available for content (between the header and the ruled-in prompt at the bottom).
pub fn bodyRows(self: *Screen) u16 {
    const used = self.headerRows() + 2 + self.prompt_rows; // a rule above the input block, one below
    return if (self.rows > used) self.rows - used else 0;
}

/// How many rows the input block occupies; the line editor sets this as a typed line wraps, so the
/// output above shortens instead of being written over. Capped, so a long paste cannot swallow it.
pub fn setPromptRows(self: *Screen, want: u16) void {
    const max: u16 = @max(@as(u16, 1), @min(@as(u16, 8), self.rows / 2));
    const n = @max(@as(u16, 1), @min(want, max));
    if (n == self.prompt_rows) return;
    const was = self.statusRow();
    self.prompt_rows = n;
    self.clampScroll();
    if (!diff.plain()) return self.fullPaint();
    // The rule and the input block moved; the rows they left or took are not what a paint drew.
    frame.begin(self);
    defer frame.end(self);
    diff.forget(self, @min(was, self.statusRow()), self.rows);
    self.skip_unchanged = true;
    defer self.skip_unchanged = false;
    self.repaint();
}

/// Take the input block's freshly painted rows, so a drag can select the line being typed
/// (the row the mouse most wants and the only one the scrollback never sees).
pub fn setPromptText(self: *Screen, rows: []const []const u8) void {
    if (rows.len == self.prompt_lines.items.len) same: {
        for (rows, self.prompt_lines.items) |r, have| if (!std.mem.eql(u8, r, have)) break :same;
        return; // a keystroke that moved only the cursor: the rows are what they were
    }
    for (self.prompt_lines.items) |l| self.gpa.free(l);
    self.prompt_lines.clearRetainingCapacity();
    for (rows) |r| {
        const dup = self.gpa.dupe(u8, r) catch return;
        self.prompt_lines.append(self.gpa, dup) catch {
            self.gpa.free(dup);
            return;
        };
    }
}

/// The text on screen row `row`: a scrollback line, or one of the input rows.
pub fn lineAtRow(self: *Screen, row: u16) ?[]const u8 {
    if (row >= self.promptRow()) {
        const i: usize = row - self.promptRow();
        return if (i < self.prompt_lines.items.len) self.prompt_lines.items[i] else null;
    }
    if (row < self.bodyTop() or row > self.bodyBottom()) return null;
    const w = self.window();
    const idx = w.first + (row - self.bodyTop());
    return if (idx < w.end) self.lines.at(idx) else null;
}

/// Rows a drag may select: the output body and the input block (never the logo header).
pub fn selectableRow(self: *Screen, row: u16) bool {
    if (row >= self.bodyTop() and row <= self.bodyBottom()) return true;
    return row >= self.promptRow() and row < self.rows;
}

/// The cap `setPromptRows` clamps to — the editor uses it to decide when to scroll the
/// input inside its block instead of growing it further.
pub fn maxPromptRows(self: *Screen) u16 {
    return @max(@as(u16, 1), @min(@as(u16, 8), self.rows / 2));
}
pub fn window(self: *Screen) Window {
    const bh = self.bodyRows();
    const n = self.lines.len;
    const max_off = if (n > bh) n - bh else 0;
    const off = @min(self.scroll_off, max_off);
    const end = n - off;
    const shown = @min(@as(usize, bh), end);
    return .{ .first = end - shown, .end = end };
}
// How many content rows are actually shown right now (fewer than bodyRows when there's
// little output) — this is what lets the rule + prompt float up just below the output.
pub fn contentShown(self: *Screen) u16 {
    const w = self.window();
    return @intCast(w.end - w.first);
}
// The prompt owns the bottom row and the rule sits just above it, both PINNED to the
// bottom, with one more rule under the input; output fills the gap top-aligned. start()
// guarantees rows >= headerRows()+5.
pub fn statusRow(self: *Screen) u16 {
    return self.rows -| self.prompt_rows -| 1;
}
/// The FIRST row of the input block (it grows downward to the rule on the last row).
pub fn promptRow(self: *Screen) u16 {
    return self.rows -| self.prompt_rows;
}
/// How many rows that block currently owns — the editor clears all of them when the line
/// it was holding goes away.
pub fn promptRows(self: *Screen) u16 {
    return self.prompt_rows;
}
pub fn inHeader(self: *Screen, row: u16) bool {
    return row >= 1 and row <= self.headerRows();
}

pub fn bodyTop(self: *Screen) u16 {
    return self.headerRows() + 1;
}
pub fn bodyBottom(self: *Screen) u16 {
    return self.headerRows() + self.contentShown();
}

pub fn maxScroll(self: *Screen) usize {
    const bh = self.bodyRows();
    const n = self.lines.len;
    return if (n > bh) n - bh else 0;
}
pub fn clampScroll(self: *Screen) void {
    const m = self.maxScroll();
    if (self.scroll_off > m) self.scroll_off = m;
}

/// Wheel/PageUp/PageDown/End. `up` older, `!up` newer; `page` uses a viewport-sized step.
pub fn scroll(self: *Screen, up: bool, page: bool) void {
    self.scrollQuiet(up, page);
    self.settleScroll();
}

/// `scroll` without the paint, so a burst of queued wheel notches moves the view once and
/// `settleScroll` paints the result. The body keeps showing the old window until then.
pub fn scrollQuiet(self: *Screen, up: bool, page: bool) void {
    oweScroll(self);
    const step: usize = if (page) @max(1, self.bodyRows()) else wheel_step;
    if (up) {
        self.scroll_off = @min(self.scroll_off + step, self.maxScroll());
    } else {
        self.scroll_off -= @min(step, self.scroll_off);
    }
}

/// Back to the newest output (a line was sent), painted as a scroll: the body slides.
pub fn scrollToBottom(self: *Screen) void {
    if (self.scroll_off == 0) return;
    oweScroll(self);
    self.scroll_off = 0;
    self.settleScroll();
}

fn oweScroll(self: *Screen) void {
    if (!self.scroll_owed) {
        self.scroll_owed = true;
        self.owed_first = self.dropped + self.window().first;
        self.owed_sel = self.has_sel;
    }
    // The highlight is keyed to screen rows, so scrolling would leave it over different text.
    self.has_sel = false;
    self.sel_active = false;
    self.drag_owed = false;
}

/// Paint what the quiet scrolls owe: the body slides by the net distance and only the rows that
/// came into view are drawn. A no-op when nothing is owed or a paint has since caught up.
pub fn settleScroll(self: *Screen) void {
    if (!self.scroll_owed) return;
    frame.begin(self);
    defer frame.end(self);
    if (!diff.plain()) {
        self.paintBody();
        self.drawStatusBar();
        return;
    }
    const moved = @as(isize, @intCast(self.dropped + self.window().first)) - @as(isize, @intCast(self.owed_first));
    if (!self.owed_sel and moved != 0) diff.shiftBody(self, moved);
    diff.paintChanged(self);
}

pub fn lineIs(self: *Screen, idx: usize, expect: []const u8) bool {
    return idx < self.lines.len and std.mem.eql(u8, self.lines.at(idx), expect);
}

// header capture (reuses logo.banner via the sink)

/// Drop the blank leading/trailing lines a banner capture emits, so the block hugs its rows.
pub fn trimBlankEnds(self: *Screen, list: *std.ArrayList([]u8)) void {
    while (list.items.len != 0 and list.items[0].len == 0) {
        self.gpa.free(list.orderedRemove(0));
    }
    while (list.items.len != 0 and list.items[list.items.len - 1].len == 0) {
        self.gpa.free(list.pop().?);
    }
}

//! Mouse selection over the scrollback: the anchor/drag/end range, the text it yields, and
//! the wash the terminal paints behind it.
const screen_mod = @import("../screen.zig");
const Screen = screen_mod.Screen;
const SelRange = screen_mod.Screen.SelRange;
const std = @import("std");
const ansi = @import("../render/ansi.zig");

pub fn selActive(self: *Screen) bool {
    return self.sel_active;
}

/// Begin a drag-selection at (col,row). Ignored (and any prior highlight cleared) unless the
/// press lands on an output row — a press on the logo header stays a logo click.
pub fn selStart(self: *Screen, col: u16, row: u16) void {
    const had = self.has_sel;
    self.has_sel = false; // no highlight until an actual drag extends the selection
    self.sel_active = false;
    if (!self.selectableRow(row)) {
        if (had) self.repaintSelection(); // clear a stale highlight
        return;
    }
    self.sel_active = true;
    self.sel_ar = row;
    self.sel_ac = col;
    self.sel_hr = row;
    self.sel_hc = col;
    if (had) self.repaintSelection(); // clear a stale highlight
}

/// Extend the in-progress selection to (col,row) without painting, so a burst of queued drag
/// reports moves the head once and `settleDrag` paints the highlight where it ended.
pub fn selDragQuiet(self: *Screen, col: u16, row: u16) void {
    if (!self.sel_active) return;
    // The drag may run from the output down into the input block, so it clamps to the
    // bottom of the screen rather than to the last output row.
    self.sel_hr = std.math.clamp(row, self.bodyTop(), self.rows -| 1); // not the rule under the input
    self.sel_hc = @max(@as(u16, 1), @min(col, self.cols));
    self.has_sel = true;
    self.drag_owed = true;
}

/// Paint the live highlight the quiet drags owe. A no-op when none is owed, or when the drag
/// has since ended or been dropped (whatever did that painted it).
pub fn settleDrag(self: *Screen) void {
    if (!self.drag_owed) return;
    self.drag_owed = false;
    if (self.sel_active) self.repaintSelection();
}

/// Finish the drag: extract the highlighted text into `sel_buf` and KEEP the highlight on
/// screen. Nothing is copied until Ctrl-S. No-op for a plain click (no drag).
pub fn selEnd(self: *Screen) void {
    if (!self.sel_active) return;
    self.sel_active = false;
    if (!self.has_sel) return; // a click without a drag selects nothing
    self.extractSelection();
    self.repaintSelection(); // settle the final highlight (drag is over)
}

/// Whether a finished, still-highlighted selection is present (and thus copyable via Ctrl-S).
pub fn hasSelection(self: *Screen) bool {
    return self.has_sel and !self.sel_active;
}

/// Take the current selection's text for the clipboard and clear the highlight. "" if none.
pub fn takeSelection(self: *Screen) []const u8 {
    if (!self.has_sel) return "";
    self.has_sel = false;
    self.sel_active = false;
    self.repaintSelection();
    return self.sel_buf.items;
}

// Normalise anchor/head into reading order (top-left → bottom-right).
pub fn selNorm(self: *Screen) SelRange {
    var sr = self.sel_ar;
    var sc = self.sel_ac;
    var er = self.sel_hr;
    var ec = self.sel_hc;
    if (er < sr or (er == sr and ec < sc)) {
        sr = self.sel_hr;
        sc = self.sel_hc;
        er = self.sel_ar;
        ec = self.sel_ac;
    }
    return .{ .sr = sr, .sc = sc, .er = er, .ec = ec };
}

// The highlighted visible-column half-open range [c0,c1) for screen `row`, null when outside the
// selection. The start row runs to the line's end, the end row from its start, middles full width.
pub fn selRowCols(self: *Screen, row: u16) ?struct { c0: u16, c1: u16 } {
    if (!self.has_sel) return null;
    const s = self.selNorm();
    if (row < s.sr or row > s.er) return null;
    const c0: u16 = if (row == s.sr) s.sc - 1 else 0;
    const c1: u16 = if (row == s.er) s.ec else self.cols;
    return .{ .c0 = c0, .c1 = c1 };
}

// Gather the selected text into sel_buf: each selected row's visible characters within its column
// range, joined with newlines, trailing blanks trimmed. Visible window only — a drag does not scroll.
pub fn extractSelection(self: *Screen) void {
    self.sel_buf.clearRetainingCapacity();
    if (self.bodyRows() == 0) return;
    const s = self.selNorm();
    var row: u16 = s.sr;
    while (row <= s.er) : (row += 1) {
        const line = self.lineAtRow(row) orelse continue; // the gap above the rule
        const c0: u16 = if (row == s.sr) s.sc - 1 else 0;
        const c1: u16 = if (row == s.er) s.ec else self.cols;
        var seg: [8192]u8 = undefined;
        const text = ansi.visibleSlice(line, c0, c1, &seg);
        var tlen = text.len;
        while (tlen > 0 and text[tlen - 1] == ' ') tlen -= 1; // trim trailing spaces
        self.sel_buf.appendSlice(self.gpa, text[0..tlen]) catch return;
        if (row != s.er) self.sel_buf.append(self.gpa, '\n') catch return;
    }
}

/// Test seams: build a selection and tear the screen down without a live terminal.
pub fn selectForTest(self: *Screen, ar: u16, ac: u16, hr: u16, hc: u16) void {
    self.sel_ar = ar;
    self.sel_ac = ac;
    self.sel_hr = hr;
    self.sel_hc = hc;
    self.has_sel = true;
    self.extractSelection(); // what a finished drag leaves behind, ready for Ctrl-C/Ctrl-S
}

/// Whether a highlight is on screen at all — the editor checks before asking for the input
/// rows to be re-washed after it repaints them.
pub fn hasHighlight(self: *Screen) bool {
    return self.has_sel;
}

const testing = std.testing;

test "selection covers the INPUT rows too, not just the output above them" {
    const a = testing.allocator;
    // A screen with no tty behind it: nothing here paints, only the selection model runs.
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var s = Screen{ .gpa = a, .io = threaded.io(), .fd = -1, .rows = 10, .cols = 40 };
    defer s.freeAll();
    try s.lines.pushBack(a, try a.dupe(u8, "wrote out.png"));
    try s.header.append(a, try a.dupe(u8, "  logo"));
    s.setPromptText(&.{"> /theme hello world"});

    // The input block is the bottom row; the header is not selectable, the body and the
    // input are.
    try testing.expect(!s.selectableRow(1)); // logo header
    try testing.expect(s.selectableRow(s.bodyTop()));
    try testing.expect(s.selectableRow(s.promptRow()));

    // Drag across "hello" on the input row and take it: the typed line is what comes out.
    const row = s.promptRow();
    s.sel_ar = row;
    s.sel_ac = 10; // 1-based columns: "> /theme |hello world"
    s.sel_hr = row;
    s.sel_hc = 14;
    s.has_sel = true;
    s.extractSelection();
    try testing.expectEqualStrings("hello", s.sel_buf.items);

    // A drag that starts in the output and ends on the input carries both lines.
    s.sel_ar = s.bodyTop();
    s.sel_ac = 1;
    s.sel_hr = row;
    s.sel_hc = 8;
    s.extractSelection();
    try testing.expectEqualStrings("wrote out.png\n> /theme", s.sel_buf.items);
}

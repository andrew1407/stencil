//! Mouse selection over the scrollback: the anchor/drag/end range, the text it yields, and
//! the wash the terminal paints behind it.
const screen_mod = @import("../screen.zig");
const Screen = screen_mod.Screen;
const SelRange = screen_mod.Screen.SelRange;
const readByteTimeout = screen_mod.Screen.readByteTimeout;
const ttyWrite = screen_mod.ttyWrite;
const std = @import("std");
const logo = @import("../../logo.zig");
const ansi = @import("../ansi.zig");


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

/// Extend the in-progress selection to (col,row) and repaint the live highlight.
pub fn selDrag(self: *Screen, col: u16, row: u16) void {
    if (!self.sel_active) return;
    // The drag may run from the output down into the input block, so it clamps to the
    // bottom of the screen rather than to the last output row.
    self.sel_hr = std.math.clamp(row, self.bodyTop(), self.rows);
    self.sel_hc = @max(@as(u16, 1), @min(col, self.cols));
    self.has_sel = true;
    self.repaintSelection();
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

// The highlighted visible-column half-open range [c0,c1) (0-based) for screen `row`, or null
// when the row is outside the selection. Start row runs from its column to the line's end;
// the end row from the line start to its column; middle rows are full width.
pub fn selRowCols(self: *Screen, row: u16) ?struct { c0: u16, c1: u16 } {
    if (!self.has_sel) return null;
    const s = self.selNorm();
    if (row < s.sr or row > s.er) return null;
    const c0: u16 = if (row == s.sr) s.sc - 1 else 0;
    const c1: u16 = if (row == s.er) s.ec else self.cols;
    return .{ .c0 = c0, .c1 = c1 };
}

// Gather the selected text into sel_buf: for each selected screen row, the buffer line's
// visible characters within the row's column range, joined with newlines (trailing blanks
// trimmed). Runs against the currently-visible window (a drag doesn't scroll).
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

/// Ask the TERMINAL to paint its own selection in the live accent (OSC 17 sets the
/// highlight background; OSC 117 puts it back). Terminals without OSC 17 ignore it,
/// so it is safe to send anywhere.
pub fn setSelectionTint(self: *Screen, on: bool) void {
    if (!logo.colorEnabled()) return;
    if (!on) {
        // Put back the exact colour the terminal had, when it told us; else ask for its
        // default with the reset opcode.
        if (self.saved_hl_len != 0) {
            var b: [80]u8 = undefined;
            const seq = std.fmt.bufPrint(&b, "\x1b]17;{s}\x1b\\", .{self.saved_hl[0..self.saved_hl_len]}) catch return;
            return ttyWrite(self.fd, seq);
        }
        return ttyWrite(self.fd, "\x1b]117\x1b\\");
    }
    const rgb = logo.accentRgb();
    var buf: [40]u8 = undefined;
    const seq = std.fmt.bufPrint(&buf, "\x1b]17;#{x:0>2}{x:0>2}{x:0>2}\x1b\\", .{ rgb[0], rgb[1], rgb[2] }) catch return;
    ttyWrite(self.fd, seq);
}

/// Ask the terminal for its current highlight colour (`OSC 17;?` → `rgb:rrrr/gggg/bbbb`).
/// Best-effort with a short deadline; runs once at startup, before anything can be typed.
pub fn queryHighlight(self: *Screen) void {
    self.saved_hl_len = 0;
    if (!logo.colorEnabled()) return;
    const in = self.in_fd orelse return;
    ttyWrite(self.fd, "\x1b]17;?\x1b\\");
    // ESC ] 1 7 ; <payload> (ESC \ | BEL). Anything unexpected ends the read at once, so a
    // terminal that stays silent — or a keystroke that beat the reply — costs one poll.
    const prefix = "\x1b]17;";
    var i: usize = 0;
    while (i < prefix.len) : (i += 1) {
        const b = readByteTimeout(in, 120) orelse return;
        if (b != prefix[i]) return;
    }
    var n: usize = 0;
    while (n < self.saved_hl.len) {
        const b = readByteTimeout(in, 120) orelse return;
        if (b == 7) break; // BEL terminator
        if (b == 0x1b) { // ST: ESC \
            _ = readByteTimeout(in, 120);
            break;
        }
        self.saved_hl[n] = b;
        n += 1;
    }
    self.saved_hl_len = n;
}

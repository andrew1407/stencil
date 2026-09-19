//! Painting the prompt: where the wrapped input sits on screen, repainting it in place,
//! and clearing it out of the way before other output arrives.
const le = @import("../line_edit.zig");
const Editor = le.Editor;
const std = @import("std");
const logo = @import("../logo.zig");
const wrappedRows = le.wrappedRows;
const max_prompt_rows = le.max_prompt_rows;
const rowSlice = le.rowSlice;

pub fn writeAll(self: *Editor, bytes: []const u8) void {
    var i: usize = 0;
    while (i < bytes.len) {
        const n = std.c.write(self.fd_out, bytes[i..].ptr, bytes.len - i); // libc write (no std.posix.write)
        if (n <= 0) return;
        i += @intCast(n);
    }
}

/// The input block's wrap geometry: how many columns the FIRST row leaves for the line (the prompt
/// eats into it) and how wide each continuation row is. Null outside screen mode.
pub fn wrapGeom(self: *Editor, prompt: []const u8) ?struct { first: usize, cols: usize } {
    const s = self.screen orelse return null;
    const cols: usize = @max(@as(usize, 8), s.cols);
    return .{ .first = if (cols > prompt.len + 1) cols - prompt.len else 1, .cols = cols };
}

pub fn refresh(self: *Editor, prompt: []const u8, line: []const u8, pos: usize) void {
    // Theme the command word only if it starts with '/'; otherwise nothing in the input.
    const cmd_end: usize = if (line.len != 0 and line[0] == '/')
        (std.mem.indexOfAny(u8, line, " \t") orelse line.len)
    else
        0;
    const s = self.screen orelse return self.refreshFlat(prompt, line, pos, cmd_end);

    // Full-screen: the input WRAPS onto as many rows as it needs (the block grows upward into the
    // output) instead of scrolling a one-row window sideways, so a long prompt can be read back.
    const g = self.wrapGeom(prompt) orelse return;
    const cols = g.cols;
    const first_avail = g.first;
    const need = wrappedRows(line.len, first_avail, cols);
    const max_rows: usize = s.maxPromptRows();
    s.setPromptRows(@intCast(@min(need, max_rows)));
    // Past the cap the block stops growing and scrolls by whole rows, keeping the row the
    // cursor is on in view — the same rule the one-row window used, one dimension up.
    const cur_row = wrappedRows(pos + 1, first_avail, cols) - 1;
    const shown = @min(need, max_rows);
    const skip = if (cur_row >= shown) cur_row - shown + 1 else 0;

    const top = s.promptRow();
    // What each input row ends up showing, handed to the screen so the mouse can select
    // the line being typed (screen.setPromptText) — the scrollback never sees it.
    var shown_rows: [max_prompt_rows][]const u8 = undefined;
    var shown_n: usize = 0;
    var row: usize = 0;
    while (row < shown) : (row += 1) {
        const idx = row + skip;
        const seg = rowSlice(line, idx, first_avail, cols);
        self.gotoRow(@intCast(top + row));
        if (idx == 0) {
            self.writeAll(logo.accentReal());
            self.writeAll(prompt);
            // The accent covers the command token only.
            const split = @min(cmd_end, seg.len);
            self.writeAll(seg[0..split]);
            self.writeAll(logo.resetSeq());
            self.writeAll(seg[split..]);
            // The stored copy is plain text: the prompt plus what this row shows.
            shown_rows[shown_n] = std.fmt.bufPrint(self.prompt_row_buf[shown_n][0..], "{s}{s}", .{ prompt, seg }) catch seg;
        } else {
            self.writeAll(seg);
            shown_rows[shown_n] = seg;
        }
        if (shown_n + 1 < max_prompt_rows) shown_n += 1;
        self.writeAll("\x1b[K");
    }
    s.setPromptText(shown_rows[0..shown_n]);
    // These rows just overwrote whatever the screen had painted, including a live selection wash: put it
    // back, or a drag over the input would flash and vanish on the next event.
    if (s.hasHighlight()) s.paintPromptSelection();
    // Park the cursor where the next keystroke lands.
    const col = if (cur_row == 0) prompt.len + pos else (pos - first_avail) % cols;
    self.gotoRow(@intCast(top + (cur_row - skip)));
    if (col > 0) {
        var fbuf: [16]u8 = undefined;
        self.writeAll(std.fmt.bufPrint(&fbuf, "\x1b[{d}C", .{col}) catch return);
    }
}

/// The plain (non-full-screen) redraw: one row, the terminal's own autowrap does the rest.
pub fn refreshFlat(self: *Editor, prompt: []const u8, line: []const u8, pos: usize, cmd_end: usize) void {
    self.gotoLineStart();
    self.writeAll(logo.accentReal());
    self.writeAll(prompt);
    self.writeAll(line[0..cmd_end]);
    self.writeAll(logo.resetSeq());
    self.writeAll(line[cmd_end..]);
    self.writeAll("\x1b[K");
    self.gotoLineStart();
    const vis = prompt.len + pos;
    if (vis > 0) {
        var fbuf: [16]u8 = undefined;
        self.writeAll(std.fmt.bufPrint(&fbuf, "\x1b[{d}C", .{vis}) catch return);
    }
}

/// Move to column 1 of an absolute screen row.
pub fn gotoRow(self: *Editor, row: u16) void {
    var b: [16]u8 = undefined;
    self.writeAll(std.fmt.bufPrint(&b, "\x1b[{d};1H", .{row}) catch "\r");
}

// Park the cursor at column 1 of the input line: the fixed prompt row in screen mode,
// otherwise the current row (a bare carriage return), matching the legacy behaviour.
pub fn gotoLineStart(self: *Editor) void {
    if (self.screen) |s| {
        var b: [16]u8 = undefined;
        self.writeAll(std.fmt.bufPrint(&b, "\x1b[{d};1H", .{s.promptRow()}) catch "\r");
    } else {
        self.writeAll("\r");
    }
}

// End the current input line. In screen mode the prompt is a fixed bottom row, so a real newline
// would scroll the alt-screen and eat the pinned header — clear the row in place instead.
pub fn endPromptLine(self: *Editor) void {
    if (self.screen) |s| {
        self.clearPromptBlock();
        // Hand the rows back: the submitted line is gone, so a block that grew to fit it
        // must shrink or the output stays squeezed under a band of blank rows.
        s.setPromptRows(1);
    } else {
        self.writeAll("\r\n");
    }
}

/// Erase EVERY row the input block owns: clearing only `promptRow()` leaves a wrapped line's
/// continuation rows on screen, and nothing repaints them until the block changes height.
pub fn clearPromptBlock(self: *Editor) void {
    const s = self.screen orelse return;
    const top = s.promptRow();
    var i: u16 = 0;
    while (i < s.promptRows()) : (i += 1) {
        self.gotoRow(top + i);
        self.writeAll("\x1b[2K");
    }
    self.gotoRow(top);
}

// Make room for a line of output printed mid-edit (a paste's note): wipe the prompt row.
// The caller repaints it afterwards, exactly as the idle hook does.
pub fn clearForOutput(self: *Editor) void {
    if (self.screen != null) return self.clearPromptBlock();
    self.gotoLineStart();
    self.writeAll("\x1b[2K");
}

// Erase the prompt row, as the one-shot pre-print hook: the caller repaints right after.
pub fn clearLineTrampoline(raw: *anyopaque) void {
    const self: *Editor = @ptrCast(@alignCast(raw));
    self.clearForOutput();
}

pub fn armLineClear(self: *Editor) void {
    logo.armPrePrint(clearLineTrampoline, self);
}

pub fn listMatches(self: *Editor, completions: []const []const u8, base: []const u8) void {
    // In screen mode, emit into the scrollback (one line) so the frame/header stay put;
    // the caller redraws the prompt afterwards. Otherwise print inline under the prompt.
    if (self.screen != null) {
        for (completions) |cand| {
            if (cand.len >= base.len and std.ascii.eqlIgnoreCase(cand[0..base.len], base))
                logo.print("{s}  ", .{cand});
        }
        logo.print("\n", .{});
        return;
    }
    self.writeAll("\r\n");
    for (completions) |cand| {
        if (cand.len >= base.len and std.ascii.eqlIgnoreCase(cand[0..base.len], base)) {
            self.writeAll(cand);
            self.writeAll("  ");
        }
    }
    self.writeAll("\r\n");
}

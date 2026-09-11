//! Editing the line itself: inserting text and image markers, the word-wise deletes, moving
//! by wrapped row, history recall and tab completion.
const le = @import("../line_edit.zig");
const Editor = le.Editor;
const std = @import("std");
const History = le.History;
const commonLen = le.commonLen;
const markerBefore = le.markerBefore;
const max_marker_label = le.max_marker_label;
const rowMove = le.rowMove;
const wordLeft = le.wordLeft;
const wordRight = le.wordRight;
const copyInto = le.copyInto;
const marker_open = le.marker_open;
const setCommand = le.setCommand;

// One Backspace. On an image marker it takes the WHOLE picture back, not one byte of its
// name — the marker is one thing on screen, so it is one thing to delete.
pub fn backspace(self: *Editor, prompt: []const u8, buf: []u8, len: *usize, pos: *usize) void {
    if (pos.* == 0) return;
    if (markerBefore(buf[0..len.*], pos.*)) |start| {
        const gone = pos.* - start;
        std.mem.copyForwards(u8, buf[start .. len.* - gone], buf[pos.*..len.*]);
        pos.* = start;
        len.* -= gone;
    } else {
        std.mem.copyForwards(u8, buf[pos.* - 1 .. len.* - 1], buf[pos.*..len.*]);
        pos.* -= 1;
        len.* -= 1;
    }
    self.syncPending(buf[0..len.*]);
    self.refresh(prompt, buf[0..len.*], pos.*);
}

// Delete from the start of the word before the cursor up to the cursor (Ctrl-W / Alt-Backspace).
pub fn deleteWordBack(self: *Editor, prompt: []const u8, buf: []u8, len: *usize, pos: *usize) void {
    const start = wordLeft(buf[0..len.*], pos.*);
    if (start == pos.*) return;
    const removed = pos.* - start;
    std.mem.copyForwards(u8, buf[start .. len.* - removed], buf[pos.*..len.*]);
    len.* -= removed;
    pos.* = start;
    self.syncPending(buf[0..len.*]);
    self.refresh(prompt, buf[0..len.*], pos.*);
}

// Delete from the cursor to the end of the word ahead of it (Alt-d / modified Delete).
pub fn deleteWordFwd(self: *Editor, prompt: []const u8, buf: []u8, len: *usize, pos: *usize) void {
    const end = wordRight(buf[0..len.*], pos.*);
    if (end == pos.*) return;
    const removed = end - pos.*;
    std.mem.copyForwards(u8, buf[pos.* .. len.* - removed], buf[end..len.*]);
    len.* -= removed;
    self.syncPending(buf[0..len.*]);
    self.refresh(prompt, buf[0..len.*], pos.*);
}

// Up (older) / Down (newer) through history, stashing the fresh line on first Up.
/// Move the cursor one wrapped row up/down inside a multi-row input. False when there is
/// no such row (or no screen geometry at all), leaving Up/Down to history recall.
pub fn rowStep(self: *Editor, prompt: []const u8, line: []const u8, pos: *usize, up: bool) bool {
    const g = self.wrapGeom(prompt) orelse return false;
    const to = rowMove(line.len, pos.*, prompt.len, g.first, g.cols, up) orelse return false;
    pos.* = to;
    return true;
}

pub fn recall(self: *Editor, prompt: []const u8, buf: []u8, len: *usize, pos: *usize, hist: *History, hidx: *usize, stash: []u8, stash_len: *usize, older: bool) void {
    const items = hist.items.items;
    if (older) {
        if (hidx.* == 0) return;
        if (hidx.* == items.len) { // leaving the fresh line — park it
            const m = @min(len.*, stash.len);
            @memcpy(stash[0..m], buf[0..m]);
            stash_len.* = m;
        }
        hidx.* -= 1;
        len.* = copyInto(buf, items[hidx.*]);
    } else {
        if (hidx.* >= items.len) return;
        hidx.* += 1;
        if (hidx.* == items.len) {
            @memcpy(buf[0..stash_len.*], stash[0..stash_len.*]);
            len.* = stash_len.*;
        } else {
            len.* = copyInto(buf, items[hidx.*]);
        }
    }
    pos.* = len.*;
    self.syncPending(buf[0..len.*]); // a recalled line carries no markers: images pasted into the abandoned one go
    self.refresh(prompt, buf[0..len.*], pos.*);
}

// Tab-complete the command word against `completions`. Only fires while still typing the
// command (no whitespace yet, cursor at the end). A unique match fills it in with a
// trailing space; several matches extend to their common prefix, or list them if that
// adds nothing. A leading '/' is preserved. Names are matched case-insensitively.
pub fn complete(self: *Editor, prompt: []const u8, buf: []u8, len: *usize, pos: *usize, completions: []const []const u8) void {
    const line = buf[0..len.*];
    if (pos.* != len.* or std.mem.indexOfAny(u8, line, " \t") != null) return;
    const has_slash = line.len != 0 and line[0] == '/';
    const base = if (has_slash) line[1..] else line;

    var count: usize = 0;
    var only: []const u8 = "";
    var lcp: []const u8 = "";
    for (completions) |cand| {
        if (cand.len < base.len or !std.ascii.eqlIgnoreCase(cand[0..base.len], base)) continue;
        lcp = if (count == 0) cand else lcp[0..commonLen(lcp, cand)];
        only = cand;
        count += 1;
    }
    if (count == 0) return;

    if (count == 1) {
        len.* = setCommand(buf, has_slash, only, true); // unique: fill in + trailing space
    } else if (lcp.len > base.len) {
        len.* = setCommand(buf, has_slash, lcp, false); // extend to the common prefix
    } else {
        self.listMatches(completions, base); // ambiguous: show the options
    }
    pos.* = len.*;
    self.refresh(prompt, buf[0..len.*], pos.*);
}

// Insert `text` at the cursor (silently ignored when the line has no room left).
pub fn insertText(self: *Editor, prompt: []const u8, buf: []u8, len: *usize, pos: *usize, text: []const u8) void {
    if (len.* + text.len <= buf.len) {
        if (pos.* < len.*) std.mem.copyBackwards(u8, buf[pos.* + text.len .. len.* + text.len], buf[pos.*..len.*]);
        @memcpy(buf[pos.*..][0..text.len], text);
        len.* += text.len;
        pos.* += text.len;
    }
    self.refresh(prompt, buf[0..len.*], pos.*);
}

// Insert "[Image #N <label>]" at the cursor, spaced off the surrounding words so the line
// still reads as the sentence being written.
pub fn insertMarker(self: *Editor, prompt: []const u8, buf: []u8, len: *usize, pos: *usize, n: usize, label: []const u8) void {
    var m: [max_marker_label + 16]u8 = undefined;
    const lead: []const u8 = if (pos.* != 0 and buf[pos.* - 1] != ' ') " " else "";
    const text = std.fmt.bufPrint(&m, "{s}{s}{d} {s}] ", .{ lead, marker_open, n, label }) catch return;
    self.insertText(prompt, buf, len, pos, text);
}

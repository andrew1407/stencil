//! Pasted and dropped pictures: bracketed paste, a dropped path, the clipboard, and the
//! mouse selection — each becoming one `[image N]` marker on the line.
const le = @import("../line_edit.zig");
const Editor = le.Editor;
const std = @import("std");
const logo = @import("../logo.zig");
const screen_mod = @import("../console/screen.zig");
const double_click_ms = le.Editor.double_click_ms;
const markerEnd = le.markerEnd;
const max_marker_label = le.max_marker_label;
const max_pending_images = le.max_pending_images;
const marker_open = le.marker_open;
const max_line = le.max_line;
const sanitizeInline = le.sanitizeInline;

// Read a bracketed paste (`ESC [ 200 ~` consumed) up to the `ESC [ 201 ~` terminator and insert
// it at the cursor. Control bytes (notably newlines) become spaces, so it lands as one line.
pub fn readPaste(self: *Editor, prompt: []const u8, buf: []u8, len: *usize, pos: *usize) void {
    const start = pos.*;
    while (true) {
        const b = self.readByte() orelse break;
        if (b == 27) { // an escape inside the paste — the only one we expect is the terminator
            if ((self.readByte() orelse break) != '[') continue; // unknown → drop the introducer
            var pr: [8]u8 = undefined;
            var pn: usize = 0;
            var bb = self.readByte() orelse break;
            while ((bb >= '0' and bb <= '9') or bb == ';') {
                if (pn < pr.len) {
                    pr[pn] = bb;
                    pn += 1;
                }
                bb = self.readByte() orelse break;
            }
            if (bb == '~' and std.mem.eql(u8, pr[0..pn], "201")) break; // end of paste
            continue; // some other CSI inside the paste — ignore it
        }
        const c: u8 = if (b < 0x20 or b == 0x7f) ' ' else b; // newlines/controls → space
        if (len.* >= buf.len) continue; // buffer full — drop the rest of the paste
        if (pos.* < len.*) std.mem.copyBackwards(u8, buf[pos.* + 1 .. len.* + 1], buf[pos.*..len.*]);
        buf[pos.*] = c;
        pos.* += 1;
        len.* += 1;
    }
    // A paste that delivered NOTHING is what a terminal does when the clipboard holds only
    // an image: its paste event carries text, and there is none. Take the picture off the
    // clipboard ourselves — pressing ⌘V/Ctrl-Shift-V with an image copied means exactly
    // what Ctrl-V means here.
    if (pos.* == start and self.pending != null) {
        _ = self.attachClipboard(prompt, buf, len, pos);
        return;
    }
    self.attachPastedPath(prompt, buf, len, pos, start);
    self.syncPending(buf[0..len.*]);
    self.refresh(prompt, buf[0..len.*], pos.*);
}

// A ⌘V that pasted nothing but the path of an image FILE becomes a marker instead of the
// raw text: the terminal cannot hand over the picture itself, only its name. The host
// decides (it knows the command being typed, and whether the file is really an image) —
// when it declines, the pasted text simply stays.
pub fn attachPastedPath(self: *Editor, prompt: []const u8, buf: []u8, len: *usize, pos: *usize, start: usize) void {
    const p = self.pending orelse return;
    if (pos.* <= start) return;
    var lb: [max_marker_label]u8 = undefined;
    self.armLineClear(); // the hook may print; erase the row only then (see attachClipboard)
    const n = p.addPath(p.ctx, buf[start..pos.*], buf[0..start], &lb) orelse {
        logo.disarmPrePrint();
        return;
    };
    logo.disarmPrePrint();
    std.mem.copyForwards(u8, buf[start..], buf[pos.*..len.*]); // the path is now the picture
    len.* -= pos.* - start;
    pos.* = start;
    self.insertMarker(prompt, buf, len, pos, p.count(p.ctx), lb[0..n]);
}

// Ctrl-V: hand the clipboard to the host, which holds the picture and gives back the
// short name its marker shows. True when the chord was handled here — with no hooks
// wired the caller falls back to returning `.paste`.
pub fn attachClipboard(self: *Editor, prompt: []const u8, buf: []u8, len: *usize, pos: *usize) bool {
    const p = self.pending orelse return false;
    var lb: [max_marker_label]u8 = undefined;
    var tb: [max_line]u8 = undefined;
    // Reading the clipboard shells out and takes a moment: leave the line ON SCREEN for
    // it and erase the row only if the hook actually says something (logo's one-shot
    // pre-print hook). Blanking it up front is what made the input blink on every paste.
    self.armLineClear();
    const what = p.paste(p.ctx, buf[0..len.*], &lb, &tb);
    logo.disarmPrePrint();
    switch (what) {
        // A picture: it rides the line as a marker until Enter loads it.
        .image => |n| self.insertMarker(prompt, buf, len, pos, p.count(p.ctx), lb[0..n]),
        // Plain text on the clipboard is a plain paste — Ctrl-V is the paste key, and
        // refusing to type what was copied because it is not a picture helps nobody.
        .text => |n| self.insertText(prompt, buf, len, pos, sanitizeInline(tb[0..n])),
        .none => self.refresh(prompt, buf[0..len.*], pos.*),
    }
    return true;
}

// Copy the live mouse selection (full-screen mode) to the clipboard. False when there is
// none, so the caller can fall through to whatever the key otherwise means.
pub fn copySelection(self: *Editor) bool {
    const s = self.screen orelse return false;
    if (!s.hasSelection()) return false;
    const text = s.takeSelection();
    if (text.len != 0) {
        if (self.copy_text_cb) |cb| cb(self.logo_ctx.?, text);
    }
    return true;
}

// Ctrl-Z: take back the last image pasted into THIS line, marker and all. False when the
// line holds none, so the chord falls through to the session's own `/unpaste`.
pub fn dropLastMarker(self: *Editor, prompt: []const u8, buf: []u8, len: *usize, pos: *usize) bool {
    const p = self.pending orelse return false;
    if (p.count(p.ctx) == 0) return false;
    var start: usize = 0;
    var end: usize = 0;
    var i: usize = 0;
    while (i < len.*) {
        if (markerEnd(buf[0..len.*], i)) |e| {
            start = i;
            end = e;
            i = e;
        } else i += 1;
    }
    if (end == 0) { // markers all gone already — drop what they stood for and carry on
        p.keep(p.ctx, &.{});
        return true;
    }
    if (end < len.* and buf[end] == ' ') end += 1; // take the marker's own spacer with it
    std.mem.copyForwards(u8, buf[start..], buf[end..len.*]);
    len.* -= end - start;
    pos.* = if (pos.* >= end) pos.* - (end - start) else @min(pos.*, start);
    self.syncPending(buf[0..len.*]);
    self.refresh(prompt, buf[0..len.*], pos.*);
    return true;
}

// The line is being abandoned (Ctrl-C / Ctrl-D / Ctrl-U / a closed tty): whatever was
// pasted into it goes too — nothing was ever loaded.
pub fn abortPending(self: *Editor) void {
    const p = self.pending orelse return;
    if (p.count(p.ctx) != 0) p.keep(p.ctx, &.{});
}

// Reconcile the host's images with the markers the line actually still holds: the
// surviving ones are kept in the order they now read (and renumbered in place, so the
// first marker is always #1); the rest are dropped. Cheap, and a no-op with none pending.
pub fn syncPending(self: *Editor, line: []u8) void {
    const p = self.pending orelse return;
    if (p.count(p.ctx) == 0) return;
    var kept: [max_pending_images]usize = undefined;
    var n: usize = 0;
    var i: usize = 0;
    while (i < line.len) {
        const end = markerEnd(line, i) orelse {
            i += 1;
            continue;
        };
        const idx: usize = line[i + marker_open.len] - '1';
        if (n < kept.len and std.mem.indexOfScalar(usize, kept[0..n], idx) == null) {
            kept[n] = idx;
            n += 1;
            line[i + marker_open.len] = '0' + @as(u8, @intCast(n));
        }
        i = end;
    }
    p.keep(p.ctx, kept[0..n]);
}

// Handle an SGR mouse report (`ESC [ <` already consumed): read up to the final 'M'/'m',
// parse it, and act — wheel scrolls the scrollback; a left-click on the pinned logo ARMS a
// deferred single-click (cycle, fired by readLine after the double-click window), and a
// *second* click within that window supersedes it as a double-click (random custom colour).
// Deferring avoids the first click's animation blocking the double-click detection. Else ignored.
pub fn handleMouse(self: *Editor, prompt: []const u8, buf: []u8, len: *usize, pos: *usize, click_pending: *bool, click_at: *i64) void {
    var mb: [32]u8 = undefined;
    var mi: usize = 0;
    while (mi < mb.len) {
        const b = self.readByte() orelse break;
        mb[mi] = b;
        mi += 1;
        if (b == 'M' or b == 'm') break;
    }
    const ev = screen_mod.parseMouse(mb[0..mi]) orelse return;
    const s = self.screen orelse return;
    if (ev.isWheelUp()) {
        s.scroll(true, false);
        self.refresh(prompt, buf[0..len.*], pos.*);
    } else if (ev.isWheelDown()) {
        s.scroll(false, false);
        self.refresh(prompt, buf[0..len.*], pos.*);
    } else if (ev.isLeftDrag()) {
        s.selDrag(ev.col, ev.row); // extend the visual text selection
        self.refresh(prompt, buf[0..len.*], pos.*);
    } else if (ev.isRelease()) {
        if (s.selActive()) { // finished a drag → settle the highlight (visual only, no copy)
            s.selEnd();
            self.refresh(prompt, buf[0..len.*], pos.*);
        }
    } else if (ev.isLeftPress()) {
        if (s.inHeader(ev.row)) { // logo click: deferred single (cycle) / double (custom)
            s.pressLogo(); // immediate feedback: the logo shrinks and springs back
            const now = self.nowMs();
            if (click_pending.* and now - click_at.* <= double_click_ms) {
                click_pending.* = false;
                self.drainMouseRelease(); // eat this click's trailing release so the flourish plays
                if (self.logo_custom_cb) |cb| cb(self.logo_ctx.?);
                self.refresh(prompt, buf[0..len.*], pos.*);
            } else {
                click_pending.* = true;
                click_at.* = now;
            }
        } else { // press on an output row → begin a visual text selection
            s.selStart(ev.col, ev.row);
            self.refresh(prompt, buf[0..len.*], pos.*);
        }
    }
}

//! Painting the screen: the pinned logo header, the body rows, the status bar and the
//! selection wash — every escape sequence the full-screen console emits comes from here.
const sc = @import("../screen.zig");
const Screen = sc.Screen;
const std = @import("std");
const logo = @import("../../app/logo.zig");
const ansi = @import("../render/ansi.zig");
const logoFx = @import("../render/logoFx.zig");
const gotoRow = sc.gotoRow;
const header_pad = sc.header_pad;
const ttyWrite = sc.ttyWrite;
const gotoClear = sc.gotoClear;

pub fn fullPaint(self: *Screen) void {
    ttyWrite(self.fd, "\x1b[2J"); // full clear — only for the initial paint / a resize
    self.repaint();
}

// Repaint every region in place (per-line clears, no full-screen \x1b[2J) — used on a theme
// change so the recolour doesn't flash the whole screen.
pub fn repaint(self: *Screen) void {
    self.paintHeader();
    self.paintBody();
    self.drawStatusBar();
}

pub fn paintHeader(self: *Screen) void {
    var rb: [8192]u8 = undefined;
    for (self.header.items, 0..) |line, i| {
        gotoRow(self.fd, @intCast(i + 1));
        ttyWrite(self.fd, ansi.clip(line, self.cols, &rb));
        ttyWrite(self.fd, "\x1b[K"); // erase to end IN PLACE — no clear-then-draw blank flash
    }
}

/// Repaint everything a highlight can cover: the output body and the input block. The editor owns
/// the input rows normally, so they are redrawn here only while a selection is on them.
pub fn repaintSelection(self: *Screen) void {
    self.paintBody();
    self.paintPromptSelection();
}

/// Draw the input rows with the selection wash over them (or plain, to clear it).
pub fn paintPromptSelection(self: *Screen) void {
    var rb: [8192]u8 = undefined;
    for (self.prompt_lines.items, 0..) |line, i| {
        const row: u16 = self.promptRow() + @as(u16, @intCast(i));
        if (row > self.rows) break;
        gotoRow(self.fd, row);
        if (self.selRowCols(row)) |sel|
            ttyWrite(self.fd, ansi.clipHighlight(line, self.cols, sel.c0, sel.c1, &rb))
        else
            ttyWrite(self.fd, ansi.clip(line, self.cols, &rb));
        ttyWrite(self.fd, "\x1b[K");
    }
}

pub fn paintBody(self: *Screen) void {
    self.paintBodyCut(null, 0);
}

// `paintBody`, with the rows from scrollback index `reveal_from` on drawn only as far as
// their first `x` visible columns (`null` = the normal, whole-line paint).
pub fn paintBodyCut(self: *Screen, reveal_from: ?usize, x: u16) void {
    if (self.bodyRows() == 0) return;
    self.clampScroll();
    const w = self.window();
    // Top-aligned below the logo; each row is OVERWRITTEN in place (write + erase-to-EOL),
    // never cleared-then-drawn, so repainting unchanged text produces no blank flash.
    var rb: [8192]u8 = undefined;
    var r: u16 = self.headerRows() + 1;
    var i: usize = w.first;
    while (i < w.end) : (i += 1) {
        gotoRow(self.fd, r);
        const cut = if (reveal_from) |f| i >= f else false;
        if (cut) // an arriving row, drawn only as far as the sweep has come
            ttyWrite(self.fd, ansi.clipPrefix(self.lines.items[i], self.cols, x, &rb))
        else if (self.selRowCols(r)) |sel| // this row is (partly) selected → draw it highlighted
            ttyWrite(self.fd, ansi.clipHighlight(self.lines.items[i], self.cols, sel.c0, sel.c1, &rb))
        else
            ttyWrite(self.fd, ansi.clip(self.lines.items[i], self.cols, &rb));
        ttyWrite(self.fd, "\x1b[K");
        r += 1;
    }
    // Clear the gap between the last output row and the pinned rule (wipes old output on a
    // shrink); the rule and prompt rows are owned by drawStatusBar and the line editor.
    while (r <= self.statusRow() -| 1) : (r += 1) gotoClear(self.fd, r);
}

// A single thin full-width accent-coloured line separating the scrollback from the prompt. When
// scrolled up it carries a small dim right-aligned scroll indicator; otherwise it's a clean line.
pub fn drawStatusBar(self: *Screen) void {
    self.drawStatusBarWipe(self.cols, "");
}

// The rule, mid-recolour: its first `x` columns in the live accent and the rest in
// `old_accent` (empty = the whole rule is live, the normal draw).
pub fn drawStatusBarWipe(self: *Screen, x: u16, old_accent: []const u8) void {
    if (self.rows < 2) return;
    var buf: [8192]u8 = undefined;
    const color = logo.colorEnabled();
    var rbuf: [64]u8 = undefined;
    // Only a functional scroll indicator is shown — the how-to hints (select / copy / theme) are gone.
    const hint: []const u8 = if (self.scroll_off != 0)
        (std.fmt.bufPrint(&rbuf, " \u{2191} scrolled {d} · PgDn resumes ", .{self.scroll_off}) catch "")
    else
        "";
    const cols: usize = self.cols;
    const hint_cols = ansi.visColumns(hint);
    const rule_cols = if (cols > hint_cols) cols - hint_cols else 0;
    var fb = std.Io.Writer.fixed(&buf);
    _ = fb.print("\x1b[{d};1H\x1b[2K", .{self.statusRow()}) catch {};
    // accentReal(), not accentSeq(): written straight to the terminal, so it needs the real SGR
    // escape — the sentinel is only for scrollback text that later passes through ansi.clip().
    if (color) _ = fb.writeAll(logo.accentReal()) catch {};
    var k: usize = 0;
    while (k < rule_cols) : (k += 1) {
        // Hand over to the outgoing accent where the wipe has not reached yet.
        if (color and old_accent.len != 0 and k == @as(usize, x)) _ = fb.writeAll(old_accent) catch {};
        _ = fb.writeAll("\u{2501}") catch {}; // ━ heavy horizontal (one thin line)
    }
    if (color) _ = fb.writeAll("\x1b[0m\x1b[2m") catch {}; // dim scroll indicator
    _ = fb.writeAll(hint) catch {};
    if (color) _ = fb.writeAll("\x1b[0m") catch {};
    ttyWrite(self.fd, fb.buffered());
}

pub fn captureHeader(self: *Screen) void {
    for (self.header.items) |l| self.gpa.free(l);
    self.header.clearRetainingCapacity();
    self.hdr_pending.clearRetainingCapacity();
    self.capturing_header = true;
    logo.banner();
    self.capturing_header = false;
    self.trimBlankEnds(&self.header);
    // Locate the "S T E N C I L" wordmark (row + starting visible column) for the
    // theme-change animation, before we append the padding rows.
    self.wordmark_row = 0;
    for (self.header.items, 0..) |line, idx| {
        if (std.mem.indexOf(u8, line, logoFx.wordmark)) |off| {
            self.wordmark_row = @intCast(idx + 1); // 1-based screen row
            self.wordmark_col = @intCast(ansi.visColumns(line[0..off]) + 1);
            break;
        }
    }
    // Padding rows between the logo and the output, so text never butts against the logo.
    var p: usize = 0;
    while (p < header_pad) : (p += 1) {
        const blank = self.gpa.dupe(u8, "") catch break;
        self.header.append(self.gpa, blank) catch {
            self.gpa.free(blank);
            break;
        };
    }
}

/// Recapture the header in the new accent, sweep the new colour across the screen, then play
/// the logo animation — called after a `/theme` change or a logo click.
pub fn onThemeChanged(self: *Screen) void {
    if (!self.mouse_on) self.setSelectionTint(true); // the terminal's own highlight follows too
    // Take the outgoing header (it still carries the old accent) before recapturing, so the
    // sweep has both renderings of every logo row to splice together.
    var old_header = self.header;
    self.header = .empty;
    defer {
        for (old_header.items) |l| self.gpa.free(l);
        old_header.deinit(self.gpa);
    }
    self.captureHeader();
    logoFx.wipeRecolor(self, old_header.items);
    self.painted_accent = logo.accentRgb();
}

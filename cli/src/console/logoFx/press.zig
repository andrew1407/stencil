//! The logo button press and the theme-change recolour wipe: the new accent jumps across
//! the screen left to right (a fixed number of JUMPS, so it lasts the same at any width)
//! while the wordmark lights letter by letter and the icon turns as a clock (clock.zig).
const std = @import("std");
const logo = @import("../../logo.zig");
const ansi = @import("../ansi.zig");
const screen_mod = @import("../screen.zig");
const Screen = screen_mod.Screen;
const Span = ansi.Span;
const clock = @import("clock.zig");
const timing = @import("timing.zig");

const iconDegrees = clock.iconDegrees;
const iconSpans = clock.iconSpans;
const sleepOrAbort = timing.sleepOrAbort;
const spin = timing.spin;
const inputPending = timing.inputPending;
const Frame = timing.Frame;
const icon_cols = clock.icon_cols;

pub const wordmark = "S T E N C I L"; // the logo wordmark, animated on a theme change
const flourish_step_ms = 115; // per-letter pace of the theme-change wordmark wave
// How long the logo stays shrunk after a click — the first half of the press-to-recolour
// lag, kept just long enough to read as a button going down.
const press_ms = 95;
// The recolour sweep jumps the new colour left to right, measured in JUMPS rather than columns so
// the animation lasts the same ~2s at any width; `wipe_step_ms` is the pause between jumps.
const wipe_step_ms = 61;
const wipe_jumps = 14;
// How much faster the separator line turns over than the seam itself, in percent (400 = 4×) —
// a percentage rather than a whole multiple so it can be tuned by fractions.
const rule_wipe_pct = 178;

/// Flash the logo one cell smaller and back — the pressed state of a button, for a click on the
/// pinned logo. Plays before the accent changes; silent when the header was never captured.
pub fn pressLogo(self: *Screen) void {
    const logo_rows = self.headerRows() -| screen_mod.header_pad;
    if (logo_rows == 0) return;
    var small: std.ArrayList([]u8) = .empty;
    defer {
        for (small.items) |l| self.gpa.free(l);
        small.deinit(self.gpa);
    }
    self.hdr_pending.clearRetainingCapacity();
    self.alt_capture = &small;
    logo.bannerCompact();
    self.alt_capture = null;
    self.hdr_pending.clearRetainingCapacity();
    self.trimBlankEnds(&small);
    if (small.items.len == 0 or small.items.len > logo_rows) return;

    // Centre the shorter block in the full logo's rows so the icon shrinks towards its middle; every
    // row is padded to the icon's width and NEVER erased to EOL, so the wordmark stays untouched.
    const top: u16 = @intCast((logo_rows - small.items.len) / 2);
    const w = @min(icon_cols, self.cols);
    var rb: [8192]u8 = undefined;
    var row: u16 = 1;
    while (row <= logo_rows) : (row += 1) {
        const line: []const u8 = if (row > top and row - top <= small.items.len)
            small.items[row - top - 1]
        else
            ""; // a row the smaller icon no longer reaches — blanked, still only that far
        screen_mod.gotoRow(self.fd, row);
        screen_mod.ttyWrite(self.fd, ansi.clipPadded(line, w, &rb));
    }
    spin(self.io, press_ms); // held, not abortable: the click's own release is already queued
    self.paintHeader(); // release: back to the full-size logo
}

// The rightmost visible column carrying the accent, across the logo and the scrollback rows on
// screen — how far a recolour has anything at all to change.
fn accentReach(self: *Screen) u16 {
    var reach: u16 = 0;
    if (self.bodyRows() != 0) {
        const w = self.window();
        var i: usize = w.first;
        while (i < w.end) : (i += 1) reach = @max(reach, ansi.accentReachOf(self.lines.items[i]));
    }
    return @min(reach, self.cols);
}

// Repaint header + body + rule in the new accent by OVERWRITING in place, as a wipe travelling
// left to right; `old_header` is the outgoing rendering, needed to splice a row's accent escapes.
pub fn wipeRecolor(self: *Screen, old_header: []const []u8) void {
    var old_buf: [20]u8 = undefined;
    const old_accent = ansi.accentSgr(self.painted_accent, &old_buf);
    const new_accent = logo.accentReal();
    const wash = logo.colorEnabled() and !std.mem.eql(u8, old_accent, new_accent);

    // Both animations run off ONE clock (wash jumps + wordmark letters interleaved by due time); a
    // queued click or keystroke supersedes the whole thing. The seam stops at the accent's reach.
    const reach: u16 = @max(icon_cols, accentReach(self));
    self.wipe_reach = reach;
    const per_jump: u16 = @max(1, (reach + wipe_jumps - 1) / wipe_jumps); // columns per jump
    const letters = wordmarkLetters();
    var t: i64 = 0; // ms since the animation began
    var wash_at: i64 = 0;
    var letter_at: i64 = 0;
    var x: u16 = per_jump;
    var li: usize = 0;
    while (true) {
        const wash_left = wash and x < reach;
        const letters_left = li < letters.len;
        if (!wash_left and !letters_left) break;
        const due: i64 = if (wash_left and letters_left)
            @min(wash_at, letter_at)
        else if (wash_left) wash_at else letter_at;
        if (due > t) {
            if (sleepOrAbort(self, due - t)) break;
            t = due;
        }
        if (wash_left and wash_at <= t) {
            // Only rows that actually carry the accent are redrawn per frame — the rest
            // are identical either side of the seam.
            paintRecolored(self, old_header, x, old_accent, new_accent, true);
            x +|= per_jump;
            wash_at = t + wipe_step_ms;
            // The wash repaints the header, which includes the wordmark row — put the lit
            // letter back on top of it so the two animations don't erase each other.
            if (li > 0 and letters_left) drawWordmark(self, letters[li - 1]);
        }
        if (letters_left and letter_at <= t) {
            drawWordmark(self, letters[li]);
            li += 1;
            letter_at = t + flourish_step_ms;
        }
    }
    paintRecolored(self, old_header, self.cols, old_accent, new_accent, false); // settle: all new
    drawWordmark(self, null); // …and the wordmark back to plain, however the loop ended
}

// One frame of the wipe: every accent-carrying row's first `x` visible columns in `new_accent` and
// the rest in `old_accent` — except inside the S icon, which turns like a clock (`iconSpans`).
fn paintRecolored(self: *Screen, old_header: []const []u8, x: u16, old_accent: []const u8, new_accent: []const u8, accent_rows_only: bool) void {
    var rb: [8192]u8 = undefined;
    var spans: [3]Span = undefined;
    // One write per frame: a moving seam touches a dozen rows, and dribbling them out row by
    // row lets the terminal fall behind the clock the sweep is paced against.
    var frame = Frame{ .fd = self.fd };
    const deg = iconDegrees(self, x);
    for (self.header.items, 0..) |line, i| {
        const was = if (i < old_header.len) old_header[i] else line;
        const row: u16 = @intCast(i + 1);
        const n = iconSpans(self, row, deg, x, &spans);
        frame.at(row);
        frame.put(ansi.spliceSpans(line, was, self.cols, spans[0..n], new_accent, old_accent, &rb));
    }
    if (self.bodyRows() != 0) {
        self.clampScroll();
        const w = self.window();
        var r: u16 = self.headerRows() + 1;
        var i: usize = w.first;
        while (i < w.end) : (i += 1) {
            const line = self.lines.items[i];
            if (!accent_rows_only or ansi.carriesAccent(line)) {
                frame.at(r);
                frame.put(ansi.spliceAccent(line, line, self.cols, x, new_accent, old_accent, &rb));
            }
            r += 1;
        }
    }
    frame.flush();
    // The rule runs AHEAD of the seam — at the seam's own rate a full-width line reads as
    // slow, so it covers the width in the first `100/rule_wipe_pct` of the seam's travel.
    const span = @max(@as(u32, 1), @as(u32, self.wipe_reach));
    const ruled: u32 = @as(u32, self.cols) * @as(u32, x) * rule_wipe_pct / (span * 100);
    self.drawStatusBarWipe(@intCast(@min(@as(u32, self.cols), ruled)), old_accent);
}

// Letter cells within the 13-char wordmark ("S T E N C I L"): 0,2,4,6,8,10,12 — the wave the
// accent travels along.
fn wordmarkLetters() []const usize {
    return &[_]usize{ 0, 2, 4, 6, 8, 10, 12 };
}

// Draw the wordmark with `lit` (a cell index) in the NEW accent, or all plain when null;
// every other letter keeps its original bold default. Purely cosmetic.
fn drawWordmark(self: *Screen, lit: ?usize) void {
    if (!logo.colorEnabled()) return;
    const row = self.wordmark_row;
    if (row == 0) return;
    const accent = logo.accentReal(); // the real escape, never the sentinel (this bypasses clip)
    var buf: [256]u8 = undefined;
    var fb = std.Io.Writer.fixed(&buf);
    _ = fb.print("\x1b[{d};{d}H", .{ row, self.wordmark_col }) catch {};
    for (wordmark, 0..) |ch, ci| {
        _ = fb.writeAll("\x1b[1m") catch {}; // bold, like the normal wordmark
        if (lit == ci) _ = fb.writeAll(accent) catch {}; // active letter → accent
        _ = fb.writeByte(ch) catch {};
        _ = fb.writeAll("\x1b[0m") catch {};
    }
    screen_mod.ttyWrite(self.fd, fb.buffered());
}

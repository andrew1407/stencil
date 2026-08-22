//! Logo & output animations for the full-screen console: the logo button press, the
//! theme-change recolour wipe (seam + wordmark wave + the S icon's clock turn), and the
//! reveal sweep that types new output in from the left. All free functions over *Screen.
const std = @import("std");
const logo = @import("../logo.zig");
const ansi = @import("ansi.zig");
const screen_mod = @import("screen.zig");
const Screen = screen_mod.Screen;
const Span = ansi.Span;

pub const wordmark = "S T E N C I L"; // the logo wordmark, animated on a theme change
const flourish_step_ms = 115; // per-letter pace of the theme-change wordmark wave
// How long the logo stays shrunk after a click — the first half of the press-to-recolour
// lag, kept just long enough to read as a button going down.
const press_ms = 95;
// Columns the logo icon owns. The full banner's widest row ends at 20 and the wordmark starts
// at 24, so the press frame repaints exactly this much and leaves the wordmark alone.
const icon_cols = 21;
// The recolour sweep jumps the new colour across the screen left to right. Measured in
// JUMPS, not columns, so the animation lasts the same ~2s at any width — `wipe_step_ms`
// is the pause the eye reads between jumps.
const wipe_step_ms = 61;
const wipe_jumps = 14;
// How much faster the separator line turns over than the seam itself, in percent (400 = 4×) —
// a percentage rather than a whole multiple so it can be tuned by fractions.
const rule_wipe_pct = 178;
// The S icon recolours as a clock instead of taking the seam: a hand pivots on its middle,
// one full clockwise turn over the seam's travel. Rows count `cell_aspect` times as far as
// columns, or the hand would trace an ellipse.
const wipe_degrees = 360.0;
const cell_aspect = 2.0;
// The hand turns a shade faster than the seam (1.25×), so the icon is home at 12 o'clock
// a little before the text finishes.
const icon_speed = 1.25;
// New output sweeps in from the left, jump-by-jump like the recolour wipe, but much
// quicker — it plays on EVERY line, so the total stays ~0.1s and queued keystrokes skip it.
const reveal_step_ms = 13;
const reveal_jumps = 8;
// A BURST — output arriving with under `reveal_quiet_ms` of silence since the last sweep —
// gets the short form (a couple of jumps per line, a cascade); past `reveal_burst_max_ms`
// of continuous output the sweep gives up and the rest lands at once.
const reveal_burst_jumps = 2;
const reveal_quiet_ms = 250;
const reveal_burst_max_ms = 900;

// However slow the speed, no burst animates for longer than this — output still has to arrive.
const reveal_burst_ceiling_ms = 5000;

/// The angle of a cell from the pivot, degrees clockwise from 12 o'clock (up = 0, right =
/// 90). Rows count `cell_aspect` times as far as columns (a cell is that much taller); the
/// pivot itself answers 0, so it is swept by the first frame.
fn cellAngle(row: f64, col: f64, cy: f64, cx: f64) f64 {
    const dx = col - cx;
    const dy = (cy - row) * cell_aspect; // up is positive, like a clock face
    if (dx == 0 and dy == 0) return 0;
    const deg = std.math.radiansToDegrees(std.math.atan2(dx, dy));
    return if (deg < 0) deg + 360 else deg;
}

/// The stretches of one row that a hand `deg` into its clockwise turn has passed, written into
/// `out`. Returns how many (0, 1 or 2): two when the swept sector has come far enough round to
/// wrap back over 12 o'clock, which leaves a row above the pivot lit at both ends.
fn sweptSpansAt(row: u16, cols: u16, cy: f64, cx: f64, deg: f64, out: *[2]Span) usize {
    if (deg <= 0 or cols == 0) return 0;
    if (deg >= wipe_degrees) { // a full turn — the whole row, in one span
        out[0] = .{ .c0 = 0, .c1 = cols };
        return 1;
    }
    var n: usize = 0;
    var open = false;
    var start: u16 = 0;
    var c: u16 = 0;
    while (c < cols) : (c += 1) {
        // Cell centres: column `c` is the (c+1)-th cell, so its centre sits at c + 1.
        const passed = cellAngle(@floatFromInt(row), @as(f64, @floatFromInt(c)) + 1.0, cy, cx) < deg;
        if (passed and !open) {
            open = true;
            start = c;
        } else if (!passed and open) {
            open = false;
            out[n] = .{ .c0 = start, .c1 = c };
            n += 1;
            if (n == out.len) return n;
        }
    }
    if (open) {
        out[n] = .{ .c0 = start, .c1 = cols };
        n += 1;
    }
    return n;
}

/// A batched terminal frame: rows are accumulated and written in one go, so a whole animation
/// step lands as a single update instead of one write per row.
const Frame = struct {
    fd: std.posix.fd_t,
    buf: [16384]u8 = undefined,
    len: usize = 0,

    fn put(self: *Frame, bytes: []const u8) void {
        if (self.len + bytes.len > self.buf.len) self.flush();
        if (bytes.len > self.buf.len) { // never fits — write it straight through
            screen_mod.ttyWrite(self.fd, bytes);
            return;
        }
        @memcpy(self.buf[self.len..][0..bytes.len], bytes);
        self.len += bytes.len;
    }

    fn at(self: *Frame, row: u16) void {
        var b: [16]u8 = undefined;
        self.put(std.fmt.bufPrint(&b, "\x1b[{d};1H", .{row}) catch return);
    }

    fn flush(self: *Frame) void {
        if (self.len == 0) return;
        screen_mod.ttyWrite(self.fd, self.buf[0..self.len]);
        self.len = 0;
    }
};

/// Flash the logo one cell smaller and back — the pressed state of a button, for a click on
/// the pinned logo. Plays before the accent changes, so a click reads as "pressed, then
/// recoloured". Silent when the header was never captured.
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

    // Centre the shorter block in the full logo's rows so the icon shrinks towards its
    // middle; every row is padded to exactly the icon's width and NEVER erased to EOL —
    // the press is the icon's animation alone, the wordmark to its right stays untouched.
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

// Repaint header + body + rule in the new accent by OVERWRITING in place, as a WIPE
// travelling left to right (no all-at-once flip, no clear-then-draw flicker); the S icon
// turns like a clock instead (`iconSpans`). `old_header` is the outgoing rendering — the
// header carries literal accent escapes, so both renderings are needed to splice a row.
pub fn wipeRecolor(self: *Screen, old_header: []const []u8) void {
    var old_buf: [20]u8 = undefined;
    const old_accent = ansi.accentSgr(self.painted_accent, &old_buf);
    const new_accent = logo.accentReal();
    const wash = logo.colorEnabled() and !std.mem.eql(u8, old_accent, new_accent);

    // Both animations run off ONE clock (wash jumps + wordmark letters interleaved by
    // due time); a queued click/keystroke supersedes the whole thing. The seam stops at
    // the accent's reach — past it nothing on screen changes.
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

// One frame of the wipe: every accent-carrying row drawn with its first `x` visible columns
// in `new_accent` and the rest in `old_accent` — except inside the S icon, which turns like
// a clock (`iconSpans`) instead of being crossed by the seam.
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

// How far round the clock hand has come: one full turn over the seam's whole distance,
// taken `icon_speed` times as fast, clamped at a full turn (past that it stays home).
fn iconDegrees(self: *Screen, x: u16) f64 {
    if (self.wipe_reach == 0 or x >= self.wipe_reach) return wipe_degrees;
    const travelled = @as(f64, @floatFromInt(x)) / @as(f64, @floatFromInt(self.wipe_reach));
    return @min(wipe_degrees, wipe_degrees * travelled * icon_speed);
}

// Which columns of a header row are already in the new accent: inside the icon's own
// columns the CLOCK (`deg` into its turn) decides, past them the left-to-right seam does.
fn iconSpans(self: *Screen, row: u16, deg: f64, x: u16, out: *[3]Span) usize {
    const w = @min(icon_cols, self.cols);
    var icon: [2]Span = undefined;
    const cy = (@as(f64, @floatFromInt(self.headerRows())) + 1.0) / 2.0;
    const cx = (@as(f64, @floatFromInt(w)) + 1.0) / 2.0;
    var n = sweptSpansAt(row, w, cy, cx, deg, &icon);
    @memcpy(out[0..n], icon[0..n]);
    if (x > w) { // the seam, on the columns to the right of the icon
        out[n] = .{ .c0 = w, .c1 = @min(x, self.cols) };
        n += 1;
    }
    return n;
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

// Busy-wait ~`ms` (a plain nanosleep is coalesced by the io loop, breaking frame pacing),
// but return true early the instant input is waiting — a queued click/keystroke supersedes.
fn sleepOrAbort(self: *Screen, ms: i64) bool {
    const deadline: i96 = std.Io.Clock.now(.awake, self.io).nanoseconds + @as(i96, ms) * std.time.ns_per_ms;
    while (std.Io.Clock.now(.awake, self.io).nanoseconds < deadline) {
        if (inputPending(self)) return true;
    }
    return false;
}

// Busy-wait ~`ms`, ignoring queued input — for the press frame, whose own trailing mouse
// release is already waiting by the time it is drawn (so aborting on input would skip it).
fn spin(io: std.Io, ms: i64) void {
    const deadline: i96 = std.Io.Clock.now(.awake, io).nanoseconds + @as(i96, ms) * std.time.ns_per_ms;
    while (std.Io.Clock.now(.awake, io).nanoseconds < deadline) {}
}

// Whether the input tty has a byte ready to read right now (non-blocking poll). False when no
// input fd was wired up (e.g. tests) so the flourish just plays to completion.
fn inputPending(self: *Screen) bool {
    const fd = self.in_fd orelse return false;
    var pfd = [_]std.posix.pollfd{.{ .fd = fd, .events = std.posix.POLL.IN, .revents = 0 }};
    const ready = std.posix.poll(&pfd, 0) catch return false;
    return ready > 0;
}

// Whether new output should be swept in rather than printed at once: the user has not
// turned it off (speed 1 = no waiting at all), and there is a real terminal to animate on
// (tests drive a Screen with no fd, and must not sit through an animation nobody can see).
pub fn revealing(self: *Screen) bool {
    return self.reveal_speed < screen_mod.speed_max and self.fd >= 0;
}

// The pace one sweep runs at, in ms. All three windows scale together off the speed, so
// the shape of the animation (jump, cascade, give up) is the same at every setting and
// only its tempo changes. `factor` is 1 at the default speed of 0.5.
const Pace = struct { step_ms: i64, quiet_ms: i64, burst_max_ms: i64 };

fn pace(self: *Screen) Pace {
    const s = std.math.clamp(self.reveal_speed, screen_mod.speed_min, screen_mod.speed_max);
    const factor = (1.0 - s) / s;
    const scale = struct {
        fn f(base: comptime_int, k: f64, floor: i64, ceil: i64) i64 {
            const v: f64 = @round(@as(f64, base) * k);
            if (!(v > @as(f64, @floatFromInt(floor)))) return floor;
            const n: i64 = @intFromFloat(@min(v, @as(f64, @floatFromInt(ceil))));
            return n;
        }
    }.f;
    return .{
        .step_ms = scale(reveal_step_ms, factor, 1, reveal_burst_ceiling_ms),
        // The quiet window must outlast one line's own sweep, or every line of a burst
        // would look like the first one after a pause and get the long form again.
        .quiet_ms = scale(reveal_quiet_ms, factor, reveal_quiet_ms, reveal_burst_ceiling_ms),
        .burst_max_ms = scale(reveal_burst_max_ms, factor, reveal_burst_max_ms, reveal_burst_ceiling_ms),
    };
}

// Wait one frame; true if a keystroke is queued (skips the rest of the sweep). Short
// frames spin for exact pacing; long ones block on the input poll instead of burning a core.
fn waitFrame(self: *Screen, ms: i64) bool {
    if (ms <= 20) return sleepOrAbort(self, ms);
    const fd = self.in_fd orelse return sleepOrAbort(self, ms);
    var pfd = [_]std.posix.pollfd{.{ .fd = fd, .events = std.posix.POLL.IN, .revents = 0 }};
    const ready = std.posix.poll(&pfd, @intCast(@min(ms, @as(i64, std.math.maxInt(i32))))) catch return false;
    return ready > 0;
}

/// Sweep the last `n_new` scrollback lines in from the left, a few columns per frame. A
/// queued keystroke ends it early; the settle always draws the finished text.
pub fn revealNew(self: *Screen, n_new: usize) void {
    const ns_ms = std.time.ns_per_ms;
    const p = pace(self);
    const now = std.Io.Clock.now(.awake, self.io).nanoseconds;
    // Still inside a burst, or the first line after a pause?
    const in_burst = self.reveal_last_ns != 0 and now - self.reveal_last_ns <= p.quiet_ms * ns_ms;
    if (!in_burst) self.reveal_burst_ns = now;
    defer self.reveal_last_ns = std.Io.Clock.now(.awake, self.io).nanoseconds;
    if (in_burst and now - self.reveal_burst_ns > p.burst_max_ms * ns_ms) {
        self.paintBody(); // output has been pouring for a while — stop animating it
        return;
    }
    const jumps: u16 = if (in_burst) reveal_burst_jumps else reveal_jumps;
    const first_new = self.lines.items.len - n_new;
    // How far right the new text actually reaches — sweeping past that is dead time.
    var reach: u16 = 0;
    const w = self.window();
    var i: usize = @max(w.first, first_new);
    while (i < w.end) : (i += 1) {
        const vis: u16 = @intCast(@min(ansi.visColumns(self.lines.items[i]), @as(usize, self.cols)));
        reach = @max(reach, vis);
    }
    if (reach == 0) { // nothing visible arrived (blank lines, or all of it scrolled off)
        self.paintBody();
        return;
    }
    const per: u16 = @max(1, (reach + jumps - 1) / jumps); // columns per jump
    self.paintBodyCut(first_new, 0); // the settled rows, with the new ones still blank
    var x: u16 = per;
    while (x < reach) : (x +|= per) {
        if (waitFrame(self, p.step_ms)) break;
        paintNewRows(self, first_new, x);
    }
    self.paintBody(); // settle: the whole text, however the loop ended
}

// One frame of the sweep: only the arriving rows, each cut to its first `x` visible
// columns. Batched into a single write so a frame lands as one terminal update.
fn paintNewRows(self: *Screen, first_new: usize, x: u16) void {
    if (self.bodyRows() == 0) return;
    var rb: [8192]u8 = undefined;
    var frame = Frame{ .fd = self.fd };
    const w = self.window();
    var r: u16 = self.headerRows() + 1;
    var i: usize = w.first;
    while (i < w.end) : (i += 1) {
        if (i >= first_new) {
            frame.at(r);
            frame.put(ansi.clipPrefix(self.lines.items[i], self.cols, x, &rb));
            frame.put("\x1b[K"); // erase what the sweep has not reached yet
        }
        r += 1;
    }
    frame.flush();
}

const testing = std.testing;

test "cellAngle: a clock face — up is 0, then right, down, left" {
    // Pivot at row 10, col 10 on a 19x19 field. Rows count double (cell_aspect), so a cell
    // one row up is as far as two columns across — that is what keeps the hand's angle
    // looking like the angle it is.
    try testing.expectApproxEqAbs(@as(f64, 0), cellAngle(9, 10, 10, 10), 0.001); // straight up
    try testing.expectApproxEqAbs(@as(f64, 90), cellAngle(10, 11, 10, 10), 0.001); // right
    try testing.expectApproxEqAbs(@as(f64, 180), cellAngle(11, 10, 10, 10), 0.001); // down
    try testing.expectApproxEqAbs(@as(f64, 270), cellAngle(10, 9, 10, 10), 0.001); // left
    // One row up and two columns right = 45° once the aspect is applied (2 rows' worth up).
    try testing.expectApproxEqAbs(@as(f64, 45), cellAngle(9, 12, 10, 10), 0.001);
    // The pivot itself reads 0, so the very first frame already covers it.
    try testing.expectEqual(@as(f64, 0), cellAngle(10, 10, 10, 10));
    // Angles are always given going clockwise, never negative.
    try testing.expect(cellAngle(9, 8, 10, 10) > 270);
}

test "sweptSpansAt: the hand covers a row a quadrant at a time, and wraps at the end" {
    var sp: [2]Span = undefined;
    // Row 5 of a 11-wide field with the pivot at (5, 6): the hand's own row.
    // Nothing at 0°, everything after a full turn.
    try testing.expectEqual(@as(usize, 0), sweptSpansAt(5, 11, 5, 6, 0, &sp));
    try testing.expectEqual(@as(usize, 1), sweptSpansAt(5, 11, 5, 6, 360, &sp));
    try testing.expectEqual(Span{ .c0 = 0, .c1 = 11 }, sp[0]);

    // At 90° the hand has swept the top-right quadrant, so on the pivot's row everything from
    // the pivot rightwards is lit and nothing to its left is.
    var n = sweptSpansAt(5, 11, 5, 6, 90.001, &sp);
    try testing.expectEqual(@as(usize, 1), n);
    try testing.expectEqual(@as(u16, 5), sp[0].c0); // 0-based column of the pivot (col 6)
    try testing.expectEqual(@as(u16, 11), sp[0].c1);

    // Half past: the right side of a row BELOW the pivot has been passed, the left has not.
    n = sweptSpansAt(7, 11, 5, 6, 180, &sp);
    try testing.expectEqual(@as(usize, 1), n);
    try testing.expect(sp[0].c0 >= 5 and sp[0].c1 == 11);

    // On the last quarter the sector wraps back over 12 o'clock: a row ABOVE the pivot is lit
    // at both ends (right side swept long ago, far left just now) and dark in between. How far
    // round that takes depends on the row — the nearer the pivot, the wider its cells' angles.
    n = sweptSpansAt(3, 11, 5, 6, 330, &sp);
    try testing.expectEqual(@as(usize, 2), n);
    try testing.expectEqual(@as(u16, 0), sp[0].c0); // the far left, just passed
    try testing.expect(sp[0].c1 < sp[1].c0); // …with unswept cells between the two
    try testing.expectEqual(@as(u16, 11), sp[1].c1);

    // A zero-width screen has nothing to sweep.
    try testing.expectEqual(@as(usize, 0), sweptSpansAt(5, 0, 5, 6, 180, &sp));
}

test "reveal speed: 0.5 by default, 1 means instantly, and no terminal never animates" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var s = Screen{ .gpa = a, .io = threaded.io(), .fd = -1, .rows = 10, .cols = 40 };
    defer s.freeAllForTest();
    try testing.expectEqual(@as(f64, 0.5), s.revealSpeed()); // the default the console starts with
    // No fd = no terminal (tests, a captured sink): the sweep must not run, so a suite
    // never sits through an animation nobody can see.
    try testing.expect(!revealing(&s));
    s.fd = 1;
    try testing.expect(revealing(&s));
    s.setRevealSpeed(1.0); // '/reveal 1' — text is simply there, no waiting at all
    try testing.expect(!revealing(&s));
    s.setRevealSpeed(screen_mod.speed_min);
    try testing.expect(revealing(&s));
    // Out of range is clamped rather than refused — the command validates, this is the floor.
    s.setRevealSpeed(0.0);
    try testing.expectEqual(screen_mod.speed_min, s.revealSpeed());
    s.setRevealSpeed(4.0);
    try testing.expectEqual(screen_mod.speed_max, s.revealSpeed());
    s.fd = -1;
}

test "reveal speed: the pace scales off it, and is 1x at the default" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var s = Screen{ .gpa = a, .io = threaded.io(), .fd = -1, .rows = 10, .cols = 40 };
    defer s.freeAllForTest();
    // 0.5 → factor 1: exactly the pace the sweep had before the speed setting existed.
    const mid = pace(&s);
    try testing.expectEqual(@as(i64, reveal_step_ms), mid.step_ms);
    try testing.expectEqual(@as(i64, reveal_quiet_ms), mid.quiet_ms);
    try testing.expectEqual(@as(i64, reveal_burst_max_ms), mid.burst_max_ms);
    // Slower speed = longer frames; the burst windows grow with them so a slow line's own
    // sweep can't be mistaken for silence, and nothing exceeds the ceiling.
    s.setRevealSpeed(screen_mod.speed_min);
    const slow = pace(&s);
    try testing.expect(slow.step_ms > mid.step_ms);
    try testing.expect(slow.quiet_ms >= slow.step_ms);
    try testing.expect(slow.burst_max_ms <= reveal_burst_ceiling_ms);
    // Faster speed = shorter frames, never zero (a zero-length frame is not an animation).
    s.setRevealSpeed(0.95);
    const fast = pace(&s);
    try testing.expect(fast.step_ms >= 1 and fast.step_ms < mid.step_ms);
    // The windows never shrink below their base, or a fast setting would give up on a burst
    // sooner than one line takes to arrive.
    try testing.expect(fast.quiet_ms >= reveal_quiet_ms and fast.burst_max_ms >= reveal_burst_max_ms);
}

test "reveal pacing: a burst sweeps shorter than a lone line, and is bounded" {
    // A line arriving on its own gets the full sweep; lines pouring in get the short form,
    // or twenty prints in a row would each queue an animation behind the last.
    try testing.expect(reveal_burst_jumps >= 1 and reveal_burst_jumps < reveal_jumps);
    // However long the output runs, the animating stops after this — the cap has to be
    // longer than one full sweep, or a single line would already exceed it.
    try testing.expect(reveal_burst_max_ms > reveal_jumps * reveal_step_ms);
}

test "iconSpans: the clock turns inside the icon, the seam owns everything past it" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var s = Screen{ .gpa = a, .io = threaded.io(), .fd = -1, .rows = 24, .cols = 80 };
    defer s.freeAllForTest();
    for (0..10) |_| try s.header.append(a, try a.dupe(u8, "logo row"));
    s.wipe_reach = 40;

    var sp: [3]Span = undefined;
    // Nothing has moved yet: no clock, no seam.
    try testing.expectEqual(@as(usize, 0), iconSpans(&s, 1, 0, 0, &sp));
    // Mid-animation the seam is past the icon, so a header row carries the icon's own swept
    // stretch AND the seam's stretch — and the seam's never starts before the icon's columns
    // end, which is what leaves the wordmark beside the icon behaving exactly as before.
    const n = iconSpans(&s, 5, 90.0, 30, &sp);
    try testing.expect(n >= 1);
    const seam = sp[n - 1];
    try testing.expectEqual(@as(u16, icon_cols), seam.c0);
    try testing.expectEqual(@as(u16, 30), seam.c1);
    for (sp[0 .. n - 1]) |icon| try testing.expect(icon.c1 <= icon_cols); // the clock stays inside
    // While the seam is still crossing the icon there is no seam stretch at all — those columns
    // belong to the hand.
    const m = iconSpans(&s, 5, 45.0, icon_cols - 4, &sp);
    for (sp[0..m]) |v| try testing.expect(v.c1 <= icon_cols);
    // Settled: a full turn covers the icon in one stretch, and the seam covers the rest.
    const k = iconSpans(&s, 5, wipe_degrees, s.cols, &sp);
    try testing.expectEqual(@as(usize, 2), k);
    try testing.expectEqual(Span{ .c0 = 0, .c1 = icon_cols }, sp[0]);
    try testing.expectEqual(Span{ .c0 = icon_cols, .c1 = 80 }, sp[1]);
}

test "iconDegrees: one full turn over exactly the distance the seam travels" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var s = Screen{ .gpa = a, .io = threaded.io(), .fd = -1, .rows = 24, .cols = 80 };
    defer s.freeAllForTest();
    s.wipe_reach = 40;
    try testing.expectEqual(@as(f64, 0), iconDegrees(&s, 0)); // 12 o'clock, nothing swept
    // 1.25× the seam: half its travel has the hand a quarter past half way round.
    try testing.expectEqual(@as(f64, 225), iconDegrees(&s, 20));
    // …so the turn is finished at 80% of the travel, and stays finished after that.
    try testing.expectEqual(wipe_degrees, iconDegrees(&s, 32));
    try testing.expectEqual(wipe_degrees, iconDegrees(&s, 36));
    try testing.expectEqual(wipe_degrees, iconDegrees(&s, 40)); // the seam arrives, the hand is home
    try testing.expectEqual(wipe_degrees, iconDegrees(&s, 999)); // the settle frame, past the end
    // The hand never runs backwards or overshoots into a second turn.
    var prev: f64 = 0;
    var x: u16 = 0;
    while (x <= 40) : (x += 1) {
        const d = iconDegrees(&s, x);
        try testing.expect(d >= prev and d <= wipe_degrees);
        prev = d;
    }
    s.wipe_reach = 0; // nothing to recolour at all — no partial turn left behind
    try testing.expectEqual(wipe_degrees, iconDegrees(&s, 0));
}

//! Pure ANSI/UTF-8 string utilities for the full-screen console: visible-column
//! counting, clipping/splitting coloured lines, the selection wash, and the recolour
//! splice helpers. No terminal IO — everything renders into caller buffers.
const std = @import("std");
const logo = @import("../logo.zig");
const theme = @import("../theme.zig");

/// Count visible columns of a string: one per UTF-8 codepoint, skipping CSI/SGR escapes and
/// the zero-width accent sentinel (0x01) — colour escapes take no screen width, so counting
/// them would land computed columns far to the right.
pub fn visColumns(s: []const u8) usize {
    var n: usize = 0;
    var i: usize = 0;
    while (i < s.len) {
        const b = s[i];
        if (b == 0x01) { // accent sentinel — zero width
            i += 1;
            continue;
        }
        const esc = csiLen(s, i);
        if (esc != 0) { // CSI escape — zero width
            i += esc;
            continue;
        }
        if ((b & 0xc0) != 0x80) n += 1; // count everything but UTF-8 continuation bytes
        i += 1;
    }
    return n;
}

fn appendBytes(out: []u8, oi: *usize, s: []const u8) void {
    if (oi.* + s.len > out.len) return;
    @memcpy(out[oi.*..][0..s.len], s);
    oi.* += s.len;
}

// Selection wash opacity: the accent is applied at this fraction over the (dark) terminal
// background, so the highlight reads as a translucent tint of the theme colour rather than a solid
// fill. Lower = more transparent.
const sel_alpha_pct = 55;

/// A background SGR that washes the current accent over the terminal background at `sel_alpha_pct`%
/// — the translucent theme-colour selection highlight. There is no grey floor, so a low accent
/// stays genuinely faint. Falls back to reverse video when colour is off. Written into `buf`.
fn selHighlightSeq(buf: []u8) []const u8 {
    if (!logo.colorEnabled()) return "\x1b[7m";
    const a = logo.accentRgb();
    const mix = [3]u8{
        @intCast(@as(u16, a[0]) * sel_alpha_pct / 100),
        @intCast(@as(u16, a[1]) * sel_alpha_pct / 100),
        @intCast(@as(u16, a[2]) * sel_alpha_pct / 100),
    };
    return std.fmt.bufPrint(buf, "\x1b[48;2;{d};{d};{d}m", .{ mix[0], mix[1], mix[2] }) catch "\x1b[7m";
}

fn selHighlightOff() []const u8 {
    return if (logo.colorEnabled()) "\x1b[49m" else "\x1b[27m"; // reset bg, or leave reverse video
}

/// Render `line` clipped to `cols`, tinting visible columns `[c0,c1)` with the translucent
/// accent wash (text-selection highlight). Keeps the row's own foreground colours — the wash
/// is re-asserted after every escape so internal SGR resets don't cancel it.
pub fn clipHighlight(line: []const u8, cols: u16, c0: u16, c1: u16, out: []u8) []const u8 {
    var hbuf: [24]u8 = undefined;
    const on = selHighlightSeq(&hbuf);
    const off = selHighlightOff();
    var oi: usize = 0;
    var vis: u16 = 0;
    var i: usize = 0;
    var span = false;
    while (i < line.len and vis < cols) {
        const b = line[i];
        if (b == 0x01) { // accent sentinel → the live accent fg escape (zero width)
            appendBytes(out, &oi, logo.accentReal());
            if (span) appendBytes(out, &oi, on); // re-assert the wash the fg escape may not touch
            i += 1;
            continue;
        }
        const esc = csiLen(line, i);
        if (esc != 0) { // colour escape — keep it
            appendBytes(out, &oi, line[i .. i + esc]);
            if (span) appendBytes(out, &oi, on); // re-assert the wash after any reset in the line
            i += esc;
            continue;
        }
        const want = vis >= c0 and vis < c1;
        if (want and !span) {
            appendBytes(out, &oi, on);
            span = true;
        } else if (!want and span) {
            appendBytes(out, &oi, off);
            span = false;
        }
        const clen = @min(utf8Len(b), line.len - i);
        if (oi + clen > out.len) break;
        @memcpy(out[oi..][0..clen], line[i..][0..clen]);
        oi += clen;
        i += clen;
        vis += 1;
    }
    if (span) appendBytes(out, &oi, off);
    appendBytes(out, &oi, "\x1b[0m");
    return out[0..oi];
}

/// The plain visible characters of `line` in visible-column range `[c0,c1)` (colours/sentinel
/// stripped) — used to build the clipboard text for a selection.
pub fn visibleSlice(line: []const u8, c0: u16, c1: u16, out: []u8) []const u8 {
    var oi: usize = 0;
    var vis: u16 = 0;
    var i: usize = 0;
    while (i < line.len) {
        const b = line[i];
        if (b == 0x01) {
            i += 1;
            continue;
        }
        const esc = csiLen(line, i);
        if (esc != 0) {
            i += esc;
            continue;
        }
        if (vis >= c1) break;
        const clen = @min(utf8Len(b), line.len - i);
        if (vis >= c0) {
            if (oi + clen > out.len) break;
            @memcpy(out[oi..][0..clen], line[i..][0..clen]);
            oi += clen;
        }
        i += clen;
        vis += 1;
    }
    return out[0..oi];
}

/// Number of bytes in the UTF-8 codepoint that starts with lead byte `b`.
fn utf8Len(b: u8) usize {
    if (b < 0x80) return 1;
    if (b >= 0xf0) return 4;
    if (b >= 0xe0) return 3;
    if (b >= 0xc0) return 2;
    return 1; // stray continuation byte — treat as one
}

/// If `line[i..]` begins a CSI/SGR escape (`ESC [` … final byte 0x40..0x7e), return its length
/// in bytes; 0 otherwise. Every line-scanning helper uses this to pass escapes through as
/// zero-width. The final byte is included; an unterminated escape runs to end-of-line.
fn csiLen(line: []const u8, i: usize) usize {
    if (!(line[i] == 0x1b and i + 1 < line.len and line[i + 1] == '[')) return 0;
    var j = i + 2;
    while (j < line.len and !(line[j] >= 0x40 and line[j] <= 0x7e)) : (j += 1) {}
    if (j < line.len) j += 1; // include the final byte
    return j - i;
}

/// Clip a possibly-ANSI-coloured line to `cols` VISIBLE columns: escapes copied verbatim (no
/// width), multi-byte UTF-8 one column each, ending with a reset so colour never bleeds into
/// the next row. Written into `out`; returns the filled slice.
pub fn clip(line: []const u8, cols: u16, out: []u8) []const u8 {
    var oi: usize = 0;
    var vis: u16 = 0;
    clipCounted(line, cols, out, &oi, &vis);
    if (logo.colorEnabled()) appendBytes(out, &oi, "\x1b[0m"); // never bleed colour into the next row
    return out[0..oi];
}

/// The first `x` visible columns of `line` (clipped to `cols`), with the accent sentinel
/// expanded — one frame's worth of a line being swept in. Escapes before the cut are emitted
/// even where their columns are skipped, so the colours are exactly the full draw's.
pub fn clipPrefix(line: []const u8, cols: u16, x: u16, out: []u8) []const u8 {
    var oi: usize = 0;
    clipRange(line, 0, @min(x, cols), logo.accentReal(), out, &oi);
    if (logo.colorEnabled()) appendBytes(out, &oi, "\x1b[0m"); // never bleed colour into the next row
    return out[0..oi];
}

/// Whether a scrollback line carries the accent at all (the sentinel `clip` expands). Only these
/// change appearance during a recolour, so only these are worth repainting per sweep frame.
pub fn carriesAccent(line: []const u8) bool {
    return std.mem.indexOfScalar(u8, line, 0x01) != null;
}

/// Clip `line` to `cols` visible columns and pad it out to exactly that width with spaces. The
/// padding is what makes a narrow row erase a wider one *without* an erase-to-end-of-line, so a
/// repaint can be confined to one region of a row (the logo icon) and leave the rest standing.
pub fn clipPadded(line: []const u8, cols: u16, out: []u8) []const u8 {
    var oi: usize = 0;
    var vis: u16 = 0;
    clipCounted(line, cols, out, &oi, &vis);
    if (logo.colorEnabled()) appendBytes(out, &oi, "\x1b[0m"); // pad on the default background
    while (vis < cols) : (vis += 1) appendBytes(out, &oi, " ");
    return out[0..oi];
}

/// `clip`'s walk, reporting how many visible columns it wrote. Shared by clip and clipPadded.
fn clipCounted(line: []const u8, cols: u16, out: []u8, oi: *usize, vis: *u16) void {
    var i: usize = 0;
    while (i < line.len) {
        const b = line[i];
        if (b == 0x01) { // accent sentinel → the live accent escape (zero visible width)
            appendBytes(out, oi, logo.accentReal());
            i += 1;
            continue;
        }
        const esc = csiLen(line, i);
        if (esc != 0) { // colour/CSI escape — copied verbatim, no width
            appendBytes(out, oi, line[i .. i + esc]);
            i += esc;
            continue;
        }
        if (vis.* >= cols) break;
        const clen = @min(utf8Len(b), line.len - i);
        if (oi.* + clen > out.len) break;
        @memcpy(out[oi.*..][0..clen], line[i..][0..clen]);
        oi.* += clen;
        i += clen;
        vis.* += 1;
    }
}

/// Build the truecolor SGR escape for an RGB accent into `buf` — the outgoing colour during a
/// recolour wipe, which `logo` no longer holds by then. "" when colour output is off.
pub fn accentSgr(rgb: [3]u8, buf: []u8) []const u8 {
    if (!logo.colorEnabled()) return "";
    return std.fmt.bufPrint(buf, "\x1b[38;2;{d};{d};{d}m", .{ rgb[0], rgb[1], rgb[2] }) catch "";
}

/// Copy visible columns `[c0,c1)` of `line` into `out`, expanding the accent sentinel to
/// `accent`. Escapes before `c1` are emitted even where their columns are skipped, so the SGR
/// state at `c0` matches a full draw — what lets two renderings splice at any column.
fn clipRange(line: []const u8, c0: u16, c1: u16, accent: []const u8, out: []u8, oi: *usize) void {
    if (c0 >= c1) return; // an empty range draws nothing at all, not just no characters
    var vis: u16 = 0;
    var i: usize = 0;
    while (i < line.len and vis < c1) {
        const b = line[i];
        if (b == 0x01) { // accent sentinel → this side's accent (zero visible width)
            appendBytes(out, oi, accent);
            i += 1;
            continue;
        }
        const esc = csiLen(line, i);
        if (esc != 0) { // colour/CSI escape — copied verbatim, no width
            appendBytes(out, oi, line[i .. i + esc]);
            i += esc;
            continue;
        }
        const clen = @min(utf8Len(b), line.len - i);
        if (vis >= c0) {
            if (oi.* + clen > out.len) return;
            @memcpy(out[oi.*..][0..clen], line[i..][0..clen]);
            oi.* += clen;
        }
        i += clen;
        vis += 1;
    }
}

/// The last visible column of `line` that is painted in the accent — the sentinel opens a tinted
/// span and the next escape (the reset that follows it) closes it. 0 when the row carries none.
pub fn accentReachOf(line: []const u8) u16 {
    var vis: u16 = 0;
    var i: usize = 0;
    var tinted = false;
    var last: u16 = 0;
    while (i < line.len) {
        if (line[i] == 0x01) { // accent sentinel — opens the tinted span
            tinted = true;
            i += 1;
            continue;
        }
        const esc = csiLen(line, i);
        if (esc != 0) { // any other SGR ends it (the reset after the prefix, a new colour, …)
            tinted = false;
            i += esc;
            continue;
        }
        vis += 1;
        if (tinted) last = vis;
        i += @min(utf8Len(line[i]), line.len - i);
    }
    return last;
}

/// A half-open stretch of visible columns, 0-based: the part of a row the clock hand has
/// already passed on this frame.
pub const Span = struct { c0: u16, c1: u16 };

/// One row of a recolour wipe: the first `x` visible columns from `next` (the new-accent
/// rendering), the rest from `prev` (the old one), clipped to `cols`. The renderings carry
/// identical text, so the seam lands exactly on column `x`; sentinel rows pass the same line
/// as both, tinted per side.
pub fn spliceAccent(next: []const u8, prev: []const u8, cols: u16, x: u16, new_accent: []const u8, old_accent: []const u8, out: []u8) []const u8 {
    var oi: usize = 0;
    const cut = @min(x, cols);
    clipRange(next, 0, cut, new_accent, out, &oi);
    clipRange(prev, cut, cols, old_accent, out, &oi);
    if (logo.colorEnabled()) appendBytes(out, &oi, "\x1b[0m"); // never bleed colour into the next row
    return out[0..oi];
}

/// One row of a recolour sweep: the columns the hand has passed (`spans`) taken from `next`
/// (the new-accent rendering) and everything else from `prev` (the old one), clipped to
/// `cols`. A boundary lands exactly on its column; sentinel rows pass the same line as both.
pub fn spliceSpans(next: []const u8, prev: []const u8, cols: u16, spans: []const Span, new_accent: []const u8, old_accent: []const u8, out: []u8) []const u8 {
    var oi: usize = 0;
    var at: u16 = 0;
    for (spans) |sp| {
        const c0 = @min(sp.c0, cols);
        const c1 = @min(sp.c1, cols);
        clipRange(prev, at, c0, old_accent, out, &oi); // not reached yet
        clipRange(next, c0, c1, new_accent, out, &oi); // the hand has been here
        at = c1;
    }
    clipRange(prev, at, cols, old_accent, out, &oi);
    if (logo.colorEnabled()) appendBytes(out, &oi, "\x1b[0m"); // never bleed colour into the next row
    return out[0..oi];
}

/// The byte offset just past the first `cols` VISIBLE columns of `line` — escapes and the
/// accent sentinel carry no width, so they travel with the piece they sit in. `line.len` when
/// the whole line fits. The width walk `clip` performs, without the copying.
pub fn splitAt(line: []const u8, cols: u16) usize {
    var vis: u16 = 0;
    var i: usize = 0;
    while (i < line.len) {
        const b = line[i];
        if (b == 0x01) {
            i += 1;
            continue;
        }
        const esc = csiLen(line, i);
        if (esc != 0) {
            i += esc;
            continue;
        }
        if (vis >= cols) return i;
        i += @min(utf8Len(b), line.len - i);
        vis += 1;
    }
    return line.len;
}

const testing = std.testing;

test "clip: counts visible columns, passes ANSI through, appends reset" {
    logo.init(false, false); // colour on, severity prefixes plain
    var out: [256]u8 = undefined;
    // Plain text clipped to 3 columns keeps 3 chars + reset.
    const a = clip("hello world", 3, &out);
    try testing.expectEqualStrings("hel\x1b[0m", a);
    // A colour escape is copied verbatim and does not consume width.
    const b = clip("\x1b[31mhi\x1b[0m", 10, &out);
    try testing.expectEqualStrings("\x1b[31mhi\x1b[0m\x1b[0m", b);
    // Multi-byte glyphs count as one column each (● is 3 bytes).
    const c = clip("\u{25cf}\u{25cf}\u{25cf}\u{25cf}", 2, &out);
    try testing.expectEqualStrings("\u{25cf}\u{25cf}\x1b[0m", c);
}

test "clip: no reset appended when colour is off" {
    logo.init(true, false); // NO_COLOR
    defer logo.init(false, false);
    var out: [64]u8 = undefined;
    try testing.expectEqualStrings("abc", clip("abcdef", 3, &out));
}

test "clipHighlight: washes the selected columns in the accent, keeps the row's own colours" {
    logo.init(false, false); // colour on, severity prefixes plain
    logo.setAccent(.{ 100, 100, 100 }); // washed at 55% over black → 48;2;55;55;55
    defer logo.setAccent(theme.rgbOf(theme.default_key));
    var out: [256]u8 = undefined;
    // The red fg is preserved; the accent wash brackets visible columns [2,5).
    const r = clipHighlight("\x1b[31mabcdef\x1b[0m", 10, 2, 5, &out);
    try testing.expectEqualStrings("\x1b[31mab\x1b[48;2;55;55;55mcde\x1b[49mf\x1b[0m\x1b[0m", r);
    // A wash reaching the end closes the bg before the final reset.
    try testing.expectEqualStrings("\x1b[48;2;55;55;55mabc\x1b[49m\x1b[0m", clipHighlight("abc", 10, 0, 3, &out));
}

test "visibleSlice: plain visible characters within a column range" {
    var out: [256]u8 = undefined;
    try testing.expectEqualStrings("cd", visibleSlice("\x1b[31mabcdef\x1b[0m", 2, 4, &out));
    try testing.expectEqualStrings("abc", visibleSlice("abc", 0, 99, &out)); // clamps to text end
    try testing.expectEqualStrings("", visibleSlice("abc", 5, 9, &out)); // range past the text
}

test "splitAt: cuts on VISIBLE columns, carrying escapes and the accent sentinel along" {
    // Plain text cuts at the column count…
    try testing.expectEqual(@as(usize, 3), splitAt("abcdef", 3));
    try testing.expectEqual(@as(usize, 6), splitAt("abcdef", 10)); // fits → the whole line
    try testing.expectEqual(@as(usize, 6), splitAt("abcdef", 6));
    // …and a colour escape (or the accent sentinel) spends no width, so it rides with the
    // piece it introduces rather than counting against it.
    const colored = "\x1b[31mabc\x1b[0mdef";
    try testing.expectEqual(@as(usize, "\x1b[31mabc\x1b[0m".len), splitAt(colored, 3));
    try testing.expectEqual(@as(usize, "\x01abc".len), splitAt("\x01abcdef", 3));
    // A UTF-8 rune is one column, not one byte.
    try testing.expectEqual(@as(usize, "…é".len), splitAt("…éx", 2));
}

test "accentReachOf: how far right the accent actually reaches on a row" {
    // A `note:`-shaped row: bold, then the sentinel opens the tint, the reset after it closes it.
    try testing.expectEqual(@as(u16, 6), accentReachOf("\x1b[1m\x01note: \x1b[0mand plain text after"));
    try testing.expectEqual(@as(u16, 0), accentReachOf("no accent here at all"));
    try testing.expectEqual(@as(u16, 0), accentReachOf("")); // nothing to recolour
    // The seam has to clear the LAST tinted span, not the first.
    try testing.expectEqual(@as(u16, 7), accentReachOf("\x01ab\x1b[0mcd\x01efg\x1b[0mhi")); // efg ends at col 7
    // Multi-byte glyphs are one column each, like everywhere else.
    try testing.expectEqual(@as(u16, 2), accentReachOf("\x01\u{2501}\u{2501}\x1b[0m"));
}

test "spliceAccent: the wipe seam lands on a visible column, each side in its own accent" {
    logo.init(false, false); // colour on
    var out: [512]u8 = undefined;
    const new_a = "\x1b[38;2;1;1;1m";
    const old_a = "\x1b[38;2;9;9;9m";

    // A scrollback line (same rendering both sides): the sentinel before the seam expands to the
    // new accent, the one after it to the old — that is the colour "covering" the previous one.
    // (the old side replays the escapes it skipped over, so its span starts in the right state)
    const line = "\x01ab\x01cd";
    try testing.expectEqualStrings(
        new_a ++ "ab" ++ old_a ++ old_a ++ "cd" ++ "\x1b[0m",
        spliceAccent(line, line, 10, 2, new_a, old_a, &out),
    );
    // Seam at 0 = nothing recoloured yet; seam past the width = fully recoloured.
    try testing.expectEqualStrings(old_a ++ "ab" ++ old_a ++ "cd" ++ "\x1b[0m", spliceAccent(line, line, 10, 0, new_a, old_a, &out));
    try testing.expectEqualStrings(new_a ++ "ab" ++ new_a ++ "cd" ++ "\x1b[0m", spliceAccent(line, line, 10, 10, new_a, old_a, &out));

    // A header row carries LITERAL accent escapes, so the two renderings differ: the seam takes
    // text from whichever side owns the column, and the escapes preceding it come along.
    try testing.expectEqualStrings(
        new_a ++ "XY" ++ old_a ++ "Z" ++ "\x1b[0m",
        spliceAccent(new_a ++ "XYZ", old_a ++ "XYZ", 10, 2, new_a, old_a, &out),
    );
    // Clipping still wins over the seam: nothing past `cols` is drawn.
    try testing.expectEqualStrings(new_a ++ "ab" ++ "\x1b[0m", spliceAccent(line, line, 2, 4, new_a, old_a, &out));
}

test "spliceSpans: each stretch lands on visible columns, in its own accent" {
    logo.init(false, false); // colour on
    var out: [512]u8 = undefined;
    const new_a = "\x1b[38;2;1;1;1m";
    const old_a = "\x1b[38;2;9;9;9m";

    // A scrollback line (same rendering both sides): the sentinel inside the swept stretch
    // expands to the new accent, the one after it to the old — that is the colour "covering"
    // the previous one. (the old side replays the escapes it skipped over, so its stretch
    // starts in the right state)
    const line = "\x01ab\x01cd";
    try testing.expectEqualStrings(
        new_a ++ "ab" ++ old_a ++ old_a ++ "cd" ++ "\x1b[0m",
        spliceSpans(line, line, 10, &.{.{ .c0 = 0, .c1 = 2 }}, new_a, old_a, &out),
    );
    // Nothing swept = nothing recoloured; the whole width swept = fully recoloured.
    try testing.expectEqualStrings(old_a ++ "ab" ++ old_a ++ "cd" ++ "\x1b[0m", spliceSpans(line, line, 10, &.{}, new_a, old_a, &out));
    try testing.expectEqualStrings(new_a ++ "ab" ++ new_a ++ "cd" ++ "\x1b[0m", spliceSpans(line, line, 10, &.{.{ .c0 = 0, .c1 = 10 }}, new_a, old_a, &out));

    // TWO stretches (the wrap at the end of the turn): lit, dark, lit again on one row. Each
    // stretch replays the escapes it skipped over, so it starts in exactly the right state.
    try testing.expectEqualStrings(
        new_a ++ "a" ++ old_a ++ "b" ++ old_a ++ "c" ++ new_a ++ new_a ++ "d" ++ "\x1b[0m",
        spliceSpans(line, line, 4, &.{ .{ .c0 = 0, .c1 = 1 }, .{ .c0 = 3, .c1 = 4 } }, new_a, old_a, &out),
    );

    // A header row carries LITERAL accent escapes, so the two renderings differ: each column
    // is taken from whichever side owns it, and the escapes preceding it come along.
    try testing.expectEqualStrings(
        new_a ++ "XY" ++ old_a ++ "Z" ++ "\x1b[0m",
        spliceSpans(new_a ++ "XYZ", old_a ++ "XYZ", 10, &.{.{ .c0 = 0, .c1 = 2 }}, new_a, old_a, &out),
    );
    // Clipping still wins: nothing past `cols` is drawn, however far the stretch reaches.
    try testing.expectEqualStrings(new_a ++ "ab" ++ "\x1b[0m", spliceSpans(line, line, 2, &.{.{ .c0 = 0, .c1 = 4 }}, new_a, old_a, &out));
}

test "accentSgr: builds the outgoing accent escape, empty when colour is off" {
    var buf: [20]u8 = undefined;
    logo.init(false, false);
    try testing.expectEqualStrings("\x1b[38;2;10;20;30m", accentSgr(.{ 10, 20, 30 }, &buf));
    logo.init(true, false); // NO_COLOR
    defer logo.init(false, false);
    try testing.expectEqualStrings("", accentSgr(.{ 10, 20, 30 }, &buf));
}

test "clipPrefix: one sweep frame — the first x visible columns, colours carried" {
    logo.init(false, false); // colour on, severity prefixes plain
    var out: [256]u8 = undefined;
    // Half-way through the sweep: 5 of the line's columns are on screen, the rest are not.
    try testing.expectEqualStrings("hello\x1b[0m", clipPrefix("hello world", 40, 5, &out));
    // Column 0 draws nothing at all — the row the sweep has not started on yet.
    try testing.expectEqualStrings("\x1b[0m", clipPrefix("hello", 40, 0, &out));
    // Past the end of the line it is simply the whole line.
    try testing.expectEqualStrings("hi\x1b[0m", clipPrefix("hi", 40, 9, &out));
    // Never wider than the window, whatever the sweep asks for.
    try testing.expectEqualStrings("hel\x1b[0m", clipPrefix("hello world", 3, 9, &out));
    // An escape before the cut is emitted even though its own column is skipped, so the
    // revealed text wears exactly the colour a full draw would give it.
    try testing.expectEqualStrings("\x1b[31mhe\x1b[0m", clipPrefix("\x1b[31mhello", 40, 2, &out));
    // Multi-byte glyphs are one column each (● is 3 bytes).
    try testing.expectEqualStrings("\u{25cf}\u{25cf}\x1b[0m", clipPrefix("\u{25cf}\u{25cf}\u{25cf}", 40, 2, &out));
}

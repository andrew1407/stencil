//! Pure ANSI/UTF-8 string utilities for the full-screen console: visible-column
//! counting, clipping/splitting coloured lines, the selection wash, and the recolour
//! splice helpers. No terminal IO — everything renders into caller buffers.
const std = @import("std");
const logo = @import("../logo.zig");
const theme = @import("../theme.zig");
const scan = @import("ansi/scan.zig");
const select = @import("ansi/select.zig");
const clip_mod = @import("ansi/clip.zig");
const splice = @import("ansi/splice.zig");

pub const visColumns = scan.visColumns;
pub const carriesAccent = scan.carriesAccent;
pub const accentReachOf = scan.accentReachOf;

pub const clipHighlight = select.clipHighlight;
pub const visibleSlice = select.visibleSlice;

pub const clip = clip_mod.clip;
pub const clipPrefix = clip_mod.clipPrefix;
pub const clipPadded = clip_mod.clipPadded;
pub const accentSgr = clip_mod.accentSgr;

pub const Span = splice.Span;
pub const spliceAccent = splice.spliceAccent;
pub const spliceSpans = splice.spliceSpans;
pub const splitAt = splice.splitAt;

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

    // The sentinel before the seam expands to the new accent, the one after it to the old — the colour
    // "covering" the previous one; the old side replays the escapes it skipped over.
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

    // The sentinel inside the swept stretch expands to the new accent, the one after it to the old;
    // the old side replays the escapes it skipped, so its stretch starts in the right state.
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

test {
    _ = scan;
    _ = select;
    _ = clip_mod;
    _ = splice;
}

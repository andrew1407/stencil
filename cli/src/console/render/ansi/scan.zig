//! Reading a coloured line without painting it: visible-column counting (escapes take no
//! width), the escape/codepoint lengths every clipper steps by, and how far right a row's
//! accent actually reaches.
const std = @import("std");
const sanitize = @import("../../../safety/sanitize.zig");

/// Count visible columns: what `cellWidth` gives each codepoint, skipping CSI/SGR escapes and the
/// zero-width accent sentinel (0x01) — counting those would land computed columns far to the right.
pub fn visColumns(s: []const u8) usize {
    var n: usize = 0;
    var i: usize = 0;
    while (i < s.len) {
        if (s[i] == 0x01) { // accent sentinel — zero width
            i += 1;
            continue;
        }
        const esc = csiLen(s, i);
        if (esc != 0) { // escape — zero width
            i += esc;
            continue;
        }
        n += cellWidth(s, i);
        i += @min(utf8Len(s[i]), s.len - i);
    }
    return n;
}

pub fn appendBytes(out: []u8, oi: *usize, s: []const u8) void {
    if (oi.* + s.len > out.len) return;
    @memcpy(out[oi.*..][0..s.len], s);
    oi.* += s.len;
}

const zwj = "\u{200d}";

/// Cells a glyph takes: two for the East Asian wide and fullwidth blocks and the emoji planes, one
/// for everything else; a ZWJ and the glyph it joins on take none, so a joined emoji is as wide as
/// its first part.
pub fn cellWidth(line: []const u8, i: usize) u16 {
    if (std.mem.startsWith(u8, line[i..], zwj)) return 0;
    if (i >= zwj.len and std.mem.eql(u8, line[i - zwj.len .. i], zwj)) return 0;
    if (line[i] < 0xe1) return 1; // below U+1000 nothing is wide
    const n = utf8Len(line[i]);
    if (i + n > line.len) return 1;
    const cp = std.unicode.utf8Decode(line[i .. i + n]) catch return 1;
    for (wide) |r| if (cp >= r[0] and cp <= r[1]) return 2;
    return 1;
}

// East Asian Width W and F, by block: Hangul Jamo, CJK radicals to CJK symbols, kana to CJK
// compatibility, the ideographs, Yi, Hangul syllables, CJK compatibility, fullwidth forms, emoji.
const wide = [_][2]u21{
    .{ 0x1100, 0x115f },   .{ 0x2e80, 0x303e }, .{ 0x3041, 0x33ff }, .{ 0x3400, 0x4dbf },
    .{ 0x4e00, 0x9fff },   .{ 0xa000, 0xa4cf }, .{ 0xac00, 0xd7a3 }, .{ 0xf900, 0xfaff },
    .{ 0xfe30, 0xfe4f },   .{ 0xff00, 0xff60 }, .{ 0xffe0, 0xffe6 }, .{ 0x1f000, 0x1faff },
    .{ 0x20000, 0x3fffd },
};

pub fn utf8Len(b: u8) usize {
    if (b < 0x80) return 1;
    if (b >= 0xf0) return 4;
    if (b >= 0xe0) return 3;
    if (b >= 0xc0) return 2;
    return 1; // stray continuation byte — treat as one
}

/// Byte length of the escape at `line[i..]`, else 0: a CSI (`ESC [` … final byte 0x40..0x7e), a
/// string sequence (OSC, DCS, APC, PM, SOS) through BEL or ST, or ESC and the byte it introduces.
/// All take no width; an unterminated one runs to end-of-line. What may be drawn is `passable`'s call.
pub fn csiLen(line: []const u8, i: usize) usize {
    if (line[i] != 0x1b) return 0;
    if (i + 1 >= line.len) return 1;
    var j = i + 2;
    switch (line[i + 1]) {
        '[' => {
            while (j < line.len and !(line[j] >= 0x40 and line[j] <= 0x7e)) : (j += 1) {}
            if (j < line.len) j += 1; // include the final byte
        },
        ']', 'P', '_', '^', 'X' => while (j < line.len) : (j += 1) {
            if (line[j] == 0x07) return j + 1 - i;
            if (line[j] == 0x1b and j + 1 < line.len and line[j + 1] == '\\') return j + 2 - i;
        },
        else => return 2,
    }
    return j - i;
}

/// What of an escape `csiLen` measured may reach the terminal: a colour, or the plain placing and
/// erasing the console lays its own rows out with (`…G`, `…H`, `…K`) — never a mode switch
/// (`?1049l`), a query the terminal answers, an OSC (a clipboard write, a title) or any other escape.
pub fn passable(seq: []const u8) []const u8 {
    if (sanitize.isSgr(seq)) return seq;
    if (seq.len < 3 or seq[1] != '[') return "";
    const final = seq[seq.len - 1];
    if (final != 'G' and final != 'H' and final != 'K') return "";
    for (seq[2 .. seq.len - 1]) |c| if (!std.ascii.isDigit(c) and c != ';') return "";
    return seq;
}

pub fn carriesAccent(line: []const u8) bool {
    return std.mem.indexOfScalar(u8, line, 0x01) != null;
}

/// The last visible column of `line` painted in the accent — the sentinel opens a tinted span and
/// the next escape (the reset that follows it) closes it. 0 when the row carries none.
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
        vis += cellWidth(line, i);
        if (tinted) last = vis;
        i += @min(utf8Len(line[i]), line.len - i);
    }
    return last;
}

test "cellWidth: CJK and fullwidth forms are two cells, halfwidth kana one" {
    try std.testing.expectEqual(@as(usize, 4), visColumns("漢字"));
    try std.testing.expectEqual(@as(usize, 6), visColumns("\x1b[31mカナ\x1b[0mab"));
    try std.testing.expectEqual(@as(usize, 2), visColumns("｀")); // U+FF40, fullwidth
    try std.testing.expectEqual(@as(usize, 1), visColumns("ｪ")); // U+FF6A, halfwidth
    try std.testing.expectEqual(@as(usize, 2), visColumns("🍅"));
    try std.testing.expectEqual(@as(usize, 3), visColumns("é…·")); // narrow above U+0800 too
}

test "cellWidth: emoji are two cells, a ZWJ-joined lime is two in all" {
    const lime = "🍋\u{200d}🟩";
    var w: u16 = 0;
    var i: usize = 0;
    while (i < lime.len) : (i += utf8Len(lime[i])) w += cellWidth(lime, i);
    try std.testing.expectEqual(@as(u16, 2), w);
    try std.testing.expectEqual(@as(u16, 1), cellWidth("a", 0));
}

test "csiLen: an OSC, a bare escape or a non-colour CSI takes no width and is never drawn" {
    const osc = "\x1b]52;c;ZXZpbA==\x07";
    try std.testing.expectEqual(osc.len, csiLen(osc ++ "x", 0));
    try std.testing.expectEqualStrings("", passable(osc));
    try std.testing.expectEqual(@as(usize, 1), visColumns(osc ++ "x"));
    try std.testing.expectEqual(@as(usize, 1), visColumns("\x1b]0;title\x1b\\x"));
    try std.testing.expectEqual(@as(usize, 2), csiLen("\x1bcx", 0)); // RIS: a full reset
    try std.testing.expectEqualStrings("\x1b[31m", passable("\x1b[31m"));
    try std.testing.expectEqualStrings("\x1b[38;2;1;2;3m", passable("\x1b[38;2;1;2;3m"));
    try std.testing.expectEqualStrings("\x1b[5;1H", passable("\x1b[5;1H"));
    try std.testing.expectEqualStrings("\x1b[2K", passable("\x1b[2K"));
    try std.testing.expectEqualStrings("\x1b[12G", passable("\x1b[12G"));
    try std.testing.expectEqualStrings("", passable("\x1b[?1049l")); // leaves the alt screen
    try std.testing.expectEqualStrings("", passable("\x1b[?25h"));
    try std.testing.expectEqualStrings("", passable("\x1b[2J"));
    try std.testing.expectEqualStrings("", passable("\x1b[c")); // a query the terminal answers
    try std.testing.expectEqualStrings("", passable("\x1b[6n"));
    try std.testing.expectEqualStrings("", passable("\x1b[21t"));
}

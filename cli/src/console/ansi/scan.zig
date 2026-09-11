//! Reading a coloured line without painting it: visible-column counting (escapes take no
//! width), the escape/codepoint lengths every clipper steps by, and how far right a row's
//! accent actually reaches.
const std = @import("std");
const logo = @import("../../logo.zig");
const theme = @import("../../theme.zig");

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

pub fn appendBytes(out: []u8, oi: *usize, s: []const u8) void {
    if (oi.* + s.len > out.len) return;
    @memcpy(out[oi.*..][0..s.len], s);
    oi.* += s.len;
}

pub fn utf8Len(b: u8) usize {
    if (b < 0x80) return 1;
    if (b >= 0xf0) return 4;
    if (b >= 0xe0) return 3;
    if (b >= 0xc0) return 2;
    return 1; // stray continuation byte — treat as one
}

/// If `line[i..]` begins a CSI/SGR escape (`ESC [` … final byte 0x40..0x7e), return its length
/// in bytes; 0 otherwise. Every line-scanning helper uses this to pass escapes through as
/// zero-width. The final byte is included; an unterminated escape runs to end-of-line.
pub fn csiLen(line: []const u8, i: usize) usize {
    if (!(line[i] == 0x1b and i + 1 < line.len and line[i + 1] == '[')) return 0;
    var j = i + 2;
    while (j < line.len and !(line[j] >= 0x40 and line[j] <= 0x7e)) : (j += 1) {}
    if (j < line.len) j += 1; // include the final byte
    return j - i;
}

/// Clip a possibly-ANSI-coloured line to `cols` VISIBLE columns: escapes copied verbatim (no
/// width), multi-byte UTF-8 one column each, ending with a reset so colour never bleeds into
/// the next row. Written into `out`; returns the filled slice.
pub fn carriesAccent(line: []const u8) bool {
    return std.mem.indexOfScalar(u8, line, 0x01) != null;
}

/// Clip `line` to `cols` visible columns and pad it out to exactly that width with spaces. The
/// padding is what makes a narrow row erase a wider one *without* an erase-to-end-of-line, so a
/// repaint can be confined to one region of a row (the logo icon) and leave the rest standing.
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

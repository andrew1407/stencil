//! The mouse selection's wash: the accent laid over the terminal ground at a fixed opacity,
//! and the plain visible slice of a row within a column range.
const std = @import("std");
const logo = @import("../../../app/logo.zig");
const theme = @import("../../../app/theme.zig");
const scan = @import("scan.zig");
const skin = @import("../../../app/skin.zig");
const restyle = @import("restyle.zig");

const visColumns = scan.visColumns;
const appendBytes = scan.appendBytes;
const utf8Len = scan.utf8Len;
const csiLen = scan.csiLen;

// Selection wash opacity: the accent over the (dark) terminal background at this fraction, so the
// highlight reads as a translucent tint of the theme colour rather than a solid fill.
const sel_alpha_pct = 55;

/// A background SGR washing the current accent (or the skin's colour at `col`) over the terminal
/// background at `sel_alpha_pct`%; falls back to reverse video when colour is off.
fn selHighlightSeq(buf: []u8, col: usize) []const u8 {
    if (!logo.colorEnabled()) return "\x1b[7m";
    const a = skin.washRgb(skin.get(), col, logo.accentRgb());
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

/// Render `line` clipped to `cols`, tinting visible columns [c0,c1) with the accent wash. The wash
/// is re-asserted after every escape, so the row's own SGR resets do not cancel it.
pub fn clipHighlight(raw: []const u8, cols: u16, c0: u16, c1: u16, out: []u8) []const u8 {
    const line = restyle.restyle(raw);
    var hbuf: [24]u8 = undefined;
    var on = selHighlightSeq(&hbuf, 0);
    const per_cell = skin.traitsOf(skin.get()).paints_runs; // the rainbow skins wash each cell in its own hue
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
        if (want and per_cell) {
            on = selHighlightSeq(&hbuf, vis);
            if (span) appendBytes(out, &oi, on);
        }
        if (want and !span) {
            appendBytes(out, &oi, on);
            span = true;
        } else if (!want and span) {
            appendBytes(out, &oi, off);
            span = false;
        }
        const clen = @min(utf8Len(b), line.len - i);
        const w = scan.cellWidth(line, i);
        if (vis + w > cols or oi + clen > out.len) break;
        @memcpy(out[oi..][0..clen], line[i..][0..clen]);
        oi += clen;
        i += clen;
        vis += w;
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

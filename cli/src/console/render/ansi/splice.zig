//! The recolour splice: one row rendered half in the new accent and half in the old, with
//! the seam landing exactly on a visible column — for the wipe and the icon's clock spans.
const std = @import("std");
const logo = @import("../../../app/logo.zig");
const theme = @import("../../../app/theme.zig");
const scan = @import("scan.zig");
const clip_mod = @import("clip.zig");

const appendBytes = scan.appendBytes;
const utf8Len = scan.utf8Len;
const csiLen = scan.csiLen;
const visColumns = scan.visColumns;
const clip = clip_mod.clip;

pub fn clipRange(line: []const u8, c0: u16, c1: u16, accent: []const u8, out: []u8, oi: *usize) void {
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

/// A half-open stretch of visible columns, 0-based: the part of a row the clock hand has
/// already passed on this frame.
pub const Span = struct { c0: u16, c1: u16 };

/// One row of a recolour wipe: the first `x` visible columns from `next` (new accent), the rest from
/// `prev` (old), clipped to `cols`. Both carry identical text, so the seam lands exactly on `x`.
pub fn spliceAccent(next: []const u8, prev: []const u8, cols: u16, x: u16, new_accent: []const u8, old_accent: []const u8, out: []u8) []const u8 {
    var oi: usize = 0;
    const cut = @min(x, cols);
    clipRange(next, 0, cut, new_accent, out, &oi);
    clipRange(prev, cut, cols, old_accent, out, &oi);
    if (logo.colorEnabled()) appendBytes(out, &oi, "\x1b[0m"); // never bleed colour into the next row
    return out[0..oi];
}

/// One row of a recolour sweep: the columns the hand has passed (`spans`) from `next`, everything
/// else from `prev`, clipped to `cols`. A boundary lands exactly on its column.
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

/// The byte offset just past the first `cols` VISIBLE columns — escapes and the accent sentinel
/// carry no width, so they travel with the piece they sit in. `line.len` when the whole line fits.
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

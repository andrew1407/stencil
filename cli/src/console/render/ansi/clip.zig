//! Clipping a coloured line to a column budget: the plain clip, one sweep frame's prefix,
//! the padded row, and the outgoing accent escape a recolour writes with.
const std = @import("std");
const logo = @import("../../../app/logo.zig");
const theme = @import("../../../app/theme.zig");
const scan = @import("scan.zig");

const appendBytes = scan.appendBytes;
const utf8Len = scan.utf8Len;
const csiLen = scan.csiLen;
const clipRange = @import("splice.zig").clipRange;
const restyle = @import("restyle.zig");

pub fn clip(line: []const u8, cols: u16, out: []u8) []const u8 {
    var oi: usize = 0;
    var vis: u16 = 0;
    clipCounted(line, cols, out, &oi, &vis);
    appendBytes(out, &oi, restyle.tail()); // never bleed colour into the next row
    return out[0..oi];
}

/// The first `x` visible columns of `line` (clipped to `cols`), accent sentinel expanded. Escapes
/// before the cut are emitted even where their columns are skipped, so the colours match a full draw.
pub fn clipPrefix(line: []const u8, cols: u16, x: u16, out: []u8) []const u8 {
    var oi: usize = 0;
    clipRange(line, 0, @min(x, cols), logo.accentReal(), out, &oi);
    appendBytes(out, &oi, restyle.tail()); // never bleed colour into the next row
    return out[0..oi];
}

/// Whether a scrollback line carries the accent at all (the sentinel `clip` expands). Only these
/// change appearance during a recolour, so only these are worth repainting per sweep frame.
pub fn clipPadded(line: []const u8, cols: u16, out: []u8) []const u8 {
    var oi: usize = 0;
    var vis: u16 = 0;
    clipCounted(line, cols, out, &oi, &vis);
    appendBytes(out, &oi, restyle.tail()); // pad on the default (or the skin's) background
    while (vis < cols) : (vis += 1) appendBytes(out, &oi, " ");
    return out[0..oi];
}

/// `clip`'s walk, reporting how many visible columns it wrote. Shared by clip and clipPadded.
pub fn clipCounted(raw: []const u8, cols: u16, out: []u8, oi: *usize, vis: *u16) void {
    const line = restyle.restyle(raw);
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
        const w = scan.cellWidth(line, i);
        if (vis.* + w > cols) break;
        const clen = @min(utf8Len(b), line.len - i);
        if (oi.* + clen > out.len) break;
        @memcpy(out[oi.*..][0..clen], line[i..][0..clen]);
        oi.* += clen;
        i += clen;
        vis.* += w;
    }
}

/// Build the truecolor SGR escape for an RGB accent into `buf` — the outgoing colour during a
/// recolour wipe, which `logo` no longer holds by then. "" when colour output is off.
pub fn accentSgr(rgb: [3]u8, buf: []u8) []const u8 {
    if (!logo.colorEnabled()) return "";
    return std.fmt.bufPrint(buf, "\x1b[38;2;{d};{d};{d}m", .{ rgb[0], rgb[1], rgb[2] }) catch "";
}

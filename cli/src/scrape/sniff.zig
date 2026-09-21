//! Pixel dimensions straight from an image header — PNG/GIF/BMP/JPEG/WebP — so the
//! dimension filter can judge a candidate without decoding it.
const std = @import("std");
const image = @import("../media/image.zig");
const testing = std.testing;

pub const Sniff = struct { width: u32, height: u32, fmt: []const u8 };

fn u16be(b: []const u8, o: usize) u32 {
    return (@as(u32, b[o]) << 8) | b[o + 1];
}
fn u16le(b: []const u8, o: usize) u32 {
    return (@as(u32, b[o + 1]) << 8) | b[o];
}
fn u32be(b: []const u8, o: usize) u32 {
    return (@as(u32, b[o]) << 24) | (@as(u32, b[o + 1]) << 16) | (@as(u32, b[o + 2]) << 8) | b[o + 3];
}
fn u32le(b: []const u8, o: usize) u32 {
    return (@as(u32, b[o + 3]) << 24) | (@as(u32, b[o + 2]) << 16) | (@as(u32, b[o + 1]) << 8) | b[o];
}
fn u24le(b: []const u8, o: usize) u32 {
    return (@as(u32, b[o + 2]) << 16) | (@as(u32, b[o + 1]) << 8) | b[o];
}

pub const png_sig = [_]u8{ 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };

/// Sniff pixel dimensions + format from an image byte header (PNG / JPEG / GIF / BMP /
/// WebP). Returns null for anything it can't measure (e.g. video, SVG, truncated data).
pub fn sniff(b: []const u8) ?Sniff {
    // PNG: 8-byte signature, then a 4-byte length + "IHDR" + width/height (big-endian).
    if (b.len >= 24 and std.mem.eql(u8, b[0..8], &png_sig) and std.mem.eql(u8, b[12..16], "IHDR")) {
        return .{ .width = u32be(b, 16), .height = u32be(b, 20), .fmt = "png" };
    }
    // GIF: "GIF87a"/"GIF89a", then little-endian logical-screen width/height.
    if (b.len >= 10 and (std.mem.eql(u8, b[0..6], "GIF87a") or std.mem.eql(u8, b[0..6], "GIF89a"))) {
        return .{ .width = u16le(b, 6), .height = u16le(b, 8), .fmt = "gif" };
    }
    // BMP: "BM", then a little-endian (possibly negative) width/height in the DIB header.
    if (b.len >= 26 and b[0] == 'B' and b[1] == 'M') {
        const w: i32 = @bitCast(u32le(b, 18));
        const h: i32 = @bitCast(u32le(b, 22));
        return .{ .width = @abs(w), .height = @abs(h), .fmt = "bmp" };
    }
    // JPEG: FF D8, then walk segments to the first SOF marker.
    if (b.len >= 4 and b[0] == 0xFF and b[1] == 0xD8) {
        if (jpegDims(b)) |d| return d;
    }
    // WebP: RIFF....WEBP + a VP8 / VP8L / VP8X chunk.
    if (b.len >= 30 and std.mem.eql(u8, b[0..4], "RIFF") and std.mem.eql(u8, b[8..12], "WEBP")) {
        if (webpDims(b)) |d| return d;
    }
    return null;
}

fn jpegDims(b: []const u8) ?Sniff {
    var pos: usize = 2;
    while (pos + 9 <= b.len) {
        if (b[pos] != 0xFF) {
            pos += 1;
            continue;
        }
        const marker = b[pos + 1];
        // Standalone markers (no length payload): padding, RSTn, SOI/EOI, TEM.
        if (marker == 0xFF) {
            pos += 1;
            continue;
        }
        if (marker == 0x01 or (marker >= 0xD0 and marker <= 0xD9)) {
            pos += 2;
            continue;
        }
        // SOF0..SOF15 except the non-SOF C4 (DHT), C8 (JPG), CC (DAC).
        if (marker >= 0xC0 and marker <= 0xCF and marker != 0xC4 and marker != 0xC8 and marker != 0xCC) {
            return .{ .height = u16be(b, pos + 5), .width = u16be(b, pos + 7), .fmt = "jpg" };
        }
        const seg = u16be(b, pos + 2);
        if (seg < 2) return null;
        pos += 2 + seg;
    }
    return null;
}

fn webpDims(b: []const u8) ?Sniff {
    const tag = b[12..16];
    if (std.mem.eql(u8, tag, "VP8 ")) {
        // Lossy: 3-byte frame tag, the 3-byte start code 9D 01 2A, then 14-bit dims.
        return .{
            .width = (u16le(b, 26)) & 0x3FFF,
            .height = (u16le(b, 28)) & 0x3FFF,
            .fmt = "webp",
        };
    }
    if (std.mem.eql(u8, tag, "VP8L") and b.len >= 25) {
        // Lossless: 1-byte signature (0x2F), then packed 14-bit width-1/height-1.
        const b1 = b[21];
        const b2 = b[22];
        const b3 = b[23];
        const b4 = b[24];
        const w = 1 + (((@as(u32, b2) & 0x3F) << 8) | b1);
        const h = 1 + (((@as(u32, b4) & 0x0F) << 10) | (@as(u32, b3) << 2) | ((@as(u32, b2) & 0xC0) >> 6));
        return .{ .width = w, .height = h, .fmt = "webp" };
    }
    if (std.mem.eql(u8, tag, "VP8X")) {
        // Extended: 4-byte flags/reserved, then 24-bit width-1 / height-1 (little-endian).
        return .{ .width = 1 + u24le(b, 24), .height = 1 + u24le(b, 27), .fmt = "webp" };
    }
    return null;
}

test "sniff: PNG / GIF / BMP / JPEG / WebP headers" {
    // PNG 200x80.
    var png = [_]u8{ 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a, 0, 0, 0, 0x0d, 'I', 'H', 'D', 'R', 0, 0, 0, 200, 0, 0, 0, 80 };
    const ps = sniff(&png).?;
    try testing.expectEqual(@as(u32, 200), ps.width);
    try testing.expectEqual(@as(u32, 80), ps.height);
    try testing.expectEqualStrings("png", ps.fmt);

    // GIF 4x3 (little-endian).
    var gif = [_]u8{ 'G', 'I', 'F', '8', '9', 'a', 4, 0, 3, 0 };
    const gs = sniff(&gif).?;
    try testing.expectEqual(@as(u32, 4), gs.width);
    try testing.expectEqual(@as(u32, 3), gs.height);

    // BMP 10x20 (width @18, height @22, little-endian).
    var bmp = [_]u8{0} ** 26;
    bmp[0] = 'B';
    bmp[1] = 'M';
    bmp[18] = 10;
    bmp[22] = 20;
    const bs = sniff(&bmp).?;
    try testing.expectEqual(@as(u32, 10), bs.width);
    try testing.expectEqual(@as(u32, 20), bs.height);

    // JPEG 1280x720 via an SOF0 marker.
    var jpg = [_]u8{ 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x04, 0, 0, 0xFF, 0xC0, 0x00, 0x11, 0x08, 0x02, 0xD0, 0x05, 0x00 };
    const js = sniff(&jpg).?;
    try testing.expectEqual(@as(u32, 1280), js.width);
    try testing.expectEqual(@as(u32, 720), js.height);
    try testing.expectEqualStrings("jpg", js.fmt);

    // WebP (lossy VP8) 64x48.
    var webp = [_]u8{0} ** 30;
    @memcpy(webp[0..4], "RIFF");
    @memcpy(webp[8..12], "WEBP");
    @memcpy(webp[12..16], "VP8 ");
    webp[23] = 0x9d;
    webp[24] = 0x01;
    webp[25] = 0x2a;
    webp[26] = 64;
    webp[27] = 0;
    webp[28] = 48;
    webp[29] = 0;
    const ws = sniff(&webp).?;
    try testing.expectEqual(@as(u32, 64), ws.width);
    try testing.expectEqual(@as(u32, 48), ws.height);
    try testing.expectEqualStrings("webp", ws.fmt);

    try testing.expect(sniff("not an image") == null);
}

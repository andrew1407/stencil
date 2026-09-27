//! What image.decode does to untrusted bytes before stb sees them: a short PNG or BMP palette
//! grown to 256 black entries, a BMP shorter than its header refused, and the largest block the
//! decode may take — the twins of pystencil's codecs/png.py and codecs/bmp.py, output for output.
const std = @import("std");

const png_magic = "\x89PNG\r\n\x1a\n";
const palette_bytes = 256 * 3;
const palette_entries = 256;
const max_side = 16384; // stb_read_impl.c's STBI_MAX_DIMENSIONS

/// `bytes` as stb should see them, or null when they need nothing (see paddedPalette and
/// bmpPrepared). `error.ImageTruncated`: a BMP whose pixel rows run past the end of the file.
pub fn prepared(a: std.mem.Allocator, bytes: []const u8) !?[]u8 {
    if (std.mem.startsWith(u8, bytes, "BM")) return bmpPrepared(a, bytes);
    return paddedPalette(a, bytes);
}

/// Headroom over the largest plane: stb's fixed blocks (the JPEG decoder state is ~20 KiB).
pub const slack = 1 << 16;

const Chunk = struct { len: usize, kind: []const u8 };

// The chunk header at `at`, or null past the end or for a length the bytes cannot hold.
fn chunkAt(bytes: []const u8, at: usize) ?Chunk {
    if (at + 8 > bytes.len) return null;
    const len = std.mem.readInt(u32, bytes[at..][0..4], .big);
    if (len > bytes.len - at - 8) return null;
    return .{ .len = len, .kind = bytes[at + 4 .. at + 8] };
}

/// `bytes` with a PLTE shorter than 256 entries grown to 256 black ones, or null when it needs
/// none: stb looks an index past the PLTE up in a stack array it never set, so without this an
/// out-of-range pixel would carry old stack bytes. It decodes as opaque black instead.
pub fn paddedPalette(a: std.mem.Allocator, bytes: []const u8) !?[]u8 {
    if (bytes.len < 26 or !std.mem.startsWith(u8, bytes, png_magic) or bytes[25] != 3) return null;
    var at: usize = png_magic.len;
    while (chunkAt(bytes, at)) |ch| : (at += 12 + ch.len) {
        if (std.mem.eql(u8, ch.kind, "IDAT") or std.mem.eql(u8, ch.kind, "IEND")) return null;
        if (!std.mem.eql(u8, ch.kind, "PLTE")) continue;
        if (ch.len == 0 or ch.len >= palette_bytes or ch.len % 3 != 0) return null;
        if (at + 12 + ch.len > bytes.len) return null;
        const tail = bytes[at + 12 + ch.len ..];
        const out = try a.alloc(u8, at + 12 + palette_bytes + tail.len);
        @memcpy(out[0..at], bytes[0..at]);
        std.mem.writeInt(u32, out[at..][0..4], palette_bytes, .big);
        const body = out[at + 4 ..][0 .. 4 + palette_bytes];
        @memcpy(body[0 .. 4 + ch.len], bytes[at + 4 ..][0 .. 4 + ch.len]);
        @memset(body[4 + ch.len ..], 0);
        std.mem.writeInt(u32, out[at + 8 + palette_bytes ..][0..4], std.hash.Crc32.hash(body), .big);
        @memcpy(out[at + 12 + palette_bytes ..], tail);
        return out;
    }
    return null;
}

const BmpHeader = struct { offset: i64, info: i64, width: i64, height: i64, bpp: u16 };

// stb's reading of a BMP header: an OS/2 core header's 16-bit sides, a top-down height's magnitude.
fn bmpHeader(bytes: []const u8) BmpHeader {
    const le = std.builtin.Endian.little;
    const offset = std.mem.readInt(u32, bytes[10..14], le);
    const info = std.mem.readInt(u32, bytes[14..18], le);
    if (info == 12) return .{
        .offset = offset,
        .info = info,
        .width = std.mem.readInt(u16, bytes[18..20], le),
        .height = std.mem.readInt(u16, bytes[20..22], le),
        .bpp = std.mem.readInt(u16, bytes[24..26], le),
    };
    return .{
        .offset = offset,
        .info = info,
        .width = std.mem.readInt(i32, bytes[18..22], le),
        .height = @intCast(@abs(@as(i64, std.mem.readInt(i32, bytes[22..26], le)))),
        .bpp = std.mem.readInt(u16, bytes[28..30], le),
    };
}

/// A BMP refused when its rows run past the file (the last row's padding may be left off), or
/// with a palette of 1/4/8-bit pixels grown to 256 black entries in front of the pixels: stb
/// reads an index past the palette out of a stack array it never set. Null when it needs neither.
fn bmpPrepared(a: std.mem.Allocator, bytes: []const u8) !?[]u8 {
    if (bytes.len < 30) return error.ImageTruncated;
    const hd = bmpHeader(bytes);
    // A side out of range is refused by the decode's own checks, which say why.
    if (hd.width <= 0 or hd.height <= 0 or hd.width > max_side or hd.height > max_side) return null;
    const row = @divFloor(hd.width * hd.bpp + 7, 8);
    if (hd.offset + @divFloor(row + 3, 4) * 4 * (hd.height - 1) + row > bytes.len) return error.ImageTruncated;
    if (hd.bpp >= 16) return null;
    const entry: i64 = if (hd.info == 12) 3 else 4;
    const used = if (hd.info == 12) @divFloor(hd.offset - 38, 3) else @divFloor(hd.offset - 14 - hd.info, 4);
    if (used <= 0) return error.ImageDecodeFailed; // no palette at all for indexed pixels
    if (used >= palette_entries) return null;
    const offset: usize = @intCast(hd.offset);
    const grow: usize = @intCast((palette_entries - used) * entry);
    const out = try a.alloc(u8, bytes.len + grow);
    @memcpy(out[0..offset], bytes[0..offset]);
    std.mem.writeInt(u32, out[10..14], @intCast(offset + grow), .little);
    @memset(out[offset..][0..grow], 0);
    @memcpy(out[offset + grow ..], bytes[offset..]);
    return out;
}

/// The largest block stb may take decoding `bytes` of `width`×`height` (0×0 when the header is
/// unreadable): the input's own blocks, a 16-bit RGBA plane padded as a JPEG pads its MCUs, or
/// twice a PNG's inflated rows (interlace passes included) — stb grows its inflate by doubling.
pub fn blockCap(bytes: []const u8, width: usize, height: usize) usize {
    const input = 2 *| bytes.len +| 4096;
    const plane = 8 *| (width +| 32) *| (height +| 32);
    return @max(input, plane, 2 *| (pngRows(bytes, width, height) +| 8 *| height)) +| slack;
}

// A PNG's inflated rows as its IHDR states them, one filter byte each; 0 for any other format.
fn pngRows(bytes: []const u8, width: usize, height: usize) usize {
    if (bytes.len < 26 or !std.mem.startsWith(u8, bytes, png_magic)) return 0;
    if (!std.mem.eql(u8, bytes[8..16], "\x00\x00\x00\x0dIHDR")) return 0;
    const channels: usize = switch (bytes[25]) {
        0, 3 => 1,
        2 => 3,
        4 => 2,
        else => 4,
    };
    const bits = width *| channels *| bytes[24];
    return height *| ((bits +| 7) / 8 +| 1);
}

const testing = std.testing;

test "paddedPalette: only a short palette in a palette PNG grows, ahead of its pixels" {
    const a = testing.allocator;
    const plte = png_magic ++ "\x00\x00\x00\x0dIHDR" ++ "\x00\x00\x00\x01\x00\x00\x00\x01\x08\x03\x00\x00\x00" ++
        "CRC!" ++ "\x00\x00\x00\x03PLTE\xff\x00\x00" ++ "CRC!" ++ "\x00\x00\x00\x00IEND" ++ "CRC!";
    const grown = (try paddedPalette(a, plte)).?;
    defer a.free(grown);
    const at = png_magic.len + 25;
    try testing.expectEqual(@as(u32, palette_bytes), std.mem.readInt(u32, grown[at..][0..4], .big));
    try testing.expectEqualStrings("PLTE\xff\x00\x00", grown[at + 4 ..][0..7]);
    try testing.expect(std.mem.allEqual(u8, grown[at + 11 ..][0 .. palette_bytes - 3], 0));
    const crc = std.hash.Crc32.hash(grown[at + 4 ..][0 .. 4 + palette_bytes]);
    try testing.expectEqual(crc, std.mem.readInt(u32, grown[at + 8 + palette_bytes ..][0..4], .big));
    try testing.expectEqualStrings("\x00\x00\x00\x00IENDCRC!", grown[grown.len - 12 ..]);

    var truecolor = plte.*;
    truecolor[25] = 6;
    try testing.expect(try paddedPalette(a, &truecolor) == null);
    try testing.expect(try paddedPalette(a, "not a png at all, not even close") == null);
}

test "bmpPrepared: a short palette grows in front of the pixels; a short file is refused" {
    const a = testing.allocator;
    // 4x1 8-bit, one palette entry (red), offset 58, indices 0 200 200 0.
    const bmp = "BM\x3e\x00\x00\x00\x00\x00\x00\x00\x3a\x00\x00\x00\x28\x00\x00\x00" ++
        "\x04\x00\x00\x00\x01\x00\x00\x00\x01\x00\x08\x00" ++ "\x00" ** 24 ++
        "\x00\x00\xff\x00" ++ "\x00\xc8\xc8\x00";
    const grown = (try prepared(a, bmp)).?;
    defer a.free(grown);
    const grow = 255 * 4;
    try testing.expectEqual(@as(usize, bmp.len + grow), grown.len);
    try testing.expectEqual(@as(u32, 58 + grow), std.mem.readInt(u32, grown[10..14], .little));
    try testing.expectEqualSlices(u8, bmp[14..58], grown[14..58]);
    try testing.expect(std.mem.allEqual(u8, grown[58..][0..grow], 0));
    try testing.expectEqualSlices(u8, bmp[58..], grown[58 + grow ..]);
    try testing.expectError(error.ImageTruncated, prepared(a, bmp[0..61]));
    try testing.expectError(error.ImageTruncated, prepared(a, bmp[0..29]));
    var no_palette = bmp.*;
    no_palette[10] = 54;
    try testing.expectError(error.ImageDecodeFailed, prepared(a, &no_palette));
}

test "blockCap: a PNG may take twice its inflated rows, never less than its input or plane" {
    const ihdr = png_magic ++ "\x00\x00\x00\x0dIHDR" ++ "\x00\x00\x00\x02\x00\x00\x00\x02\x10\x06\x00\x00\x00";
    const rows = 2 * (2 * 4 * 2 + 1); // 16-bit RGBA, a filter byte a row
    try testing.expectEqual(pngRows(ihdr, 2, 2), rows);
    const big = 4000;
    try testing.expectEqual(2 * (pngRows(ihdr, big, big) + 8 * big) + slack, blockCap(ihdr, big, big));
    try testing.expectEqual(8 * 33 * 33 + slack, blockCap("jpeg", 1, 1));
    try testing.expect(blockCap("jpeg", 4096, 2048) >= 4096 * 2048 * 4 + (20 << 10));
}

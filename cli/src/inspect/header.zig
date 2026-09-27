//! What an image header says without a decode: format, pixel size and whether the pixels
//! carry alpha, for `--probe`. The size sniff is the scraper's (scrape/sniff.zig), and stb's
//! header reader covers what it misses.
const std = @import("std");
const image = @import("../media/image.zig");
const scrape = @import("../scrape.zig");
const sniff = @import("../scrape/sniff.zig");

pub const Header = struct { format: ?[]const u8, width: u32, height: u32, alpha: ?bool = null };

/// The five sniffed formats first; stb's own header reader covers the rest it decodes (tga),
/// named by `input`'s extension.
pub fn read(head: []const u8, input: []const u8) ?Header {
    if (scrape.sniff(head)) |s| {
        if (s.width != 0 and s.height != 0)
            return .{ .format = s.fmt, .width = s.width, .height = s.height, .alpha = alphaOf(head, s.fmt) };
    }
    const d = image.dims(head) orelse return null;
    const fmt: ?[]const u8 = if (image.formatOfPath(input)) |f| f.ext() else null;
    return .{ .format = fmt, .width = @intCast(d.width), .height = @intCast(d.height) };
}

/// Whether the header says the pixels carry alpha: PNG's colour type or a tRNS chunk before the
/// data, WebP's lossless or extended flag; JPEG never does; GIF and BMP cannot say.
pub fn alphaOf(b: []const u8, fmt: []const u8) ?bool {
    if (std.mem.eql(u8, fmt, "jpg")) return false;
    if (std.mem.eql(u8, fmt, "png")) {
        if (b.len < 26) return null;
        if (b[25] == 4 or b[25] == 6) return true;
        var pos: usize = 8;
        while (pos + 8 <= b.len) {
            const kind = b[pos + 4 .. pos + 8];
            if (std.mem.eql(u8, kind, "tRNS")) return true;
            if (std.mem.eql(u8, kind, "IDAT") or std.mem.eql(u8, kind, "IEND")) break;
            pos += 12 + std.mem.readInt(u32, b[pos..][0..4], .big);
        }
        return false;
    }
    if (std.mem.eql(u8, fmt, "webp")) {
        const tag = b[12..16];
        if (std.mem.eql(u8, tag, "VP8 ")) return false;
        if (std.mem.eql(u8, tag, "VP8L") and b.len > 24) return b[24] & 0x10 != 0;
        if (std.mem.eql(u8, tag, "VP8X")) return b[20] & 0x10 != 0;
    }
    return null;
}

const testing = std.testing;

fn pngHead(color_type: u8, trns: bool) [51]u8 {
    var b = [_]u8{0} ** 51;
    @memcpy(b[0..8], &sniff.png_sig);
    b[11] = 13;
    @memcpy(b[12..16], "IHDR");
    b[19] = 40; // width 40
    b[23] = 30; // height 30
    b[25] = color_type;
    @memcpy(b[37..41], if (trns) "tRNS" else "IDAT");
    return b;
}

test "alphaOf: PNG colour type and tRNS, WebP flags, JPEG never, GIF/BMP unknown" {
    try testing.expectEqual(@as(?bool, true), alphaOf(&pngHead(6, false), "png"));
    try testing.expectEqual(@as(?bool, false), alphaOf(&pngHead(2, false), "png"));
    try testing.expectEqual(@as(?bool, true), alphaOf(&pngHead(3, true), "png"));
    try testing.expectEqual(@as(?bool, false), alphaOf("\xff\xd8", "jpg"));
    try testing.expectEqual(@as(?bool, null), alphaOf("GIF89a", "gif"));

    var webp = [_]u8{0} ** 30;
    @memcpy(webp[12..16], "VP8X");
    webp[20] = 0x10;
    try testing.expectEqual(@as(?bool, true), alphaOf(&webp, "webp"));
    @memcpy(webp[12..16], "VP8 ");
    try testing.expectEqual(@as(?bool, false), alphaOf(&webp, "webp"));
}

test "read: a sniffed header answers, anything else is not an image" {
    const h = read(&pngHead(6, false), "a.png").?;
    try testing.expectEqualStrings("png", h.format.?);
    try testing.expectEqual(@as(u32, 40), h.width);
    try testing.expectEqual(@as(u32, 30), h.height);
    try testing.expectEqual(@as(?bool, true), h.alpha);
    try testing.expect(read("not an image", "a.png") == null);
}

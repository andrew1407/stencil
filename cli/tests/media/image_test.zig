//! The stb codec layer: a format read from an extension, a decode that hands stb's own buffer
//! over with no temporary left behind, a JPEG cut mid-scan decoding to pinned zero-filled
//! pixels, and an encode/decode round trip.
const std = @import("std");
const image = @import("../../src/media/image.zig");
const imageAlloc = @import("../../src/media/imageAlloc.zig");
const Rgba8 = image.Rgba8;
const decode = image.decode;
const encode = image.encode;
const formatFromExt = image.formatFromExt;
const testing = std.testing;

test "formatFromExt" {
    try testing.expect(formatFromExt("PNG").? == .png);
    try testing.expect(formatFromExt(".jpeg").? == .jpeg);
    try testing.expect(formatFromExt("jpg").? == .jpeg);
    try testing.expect(formatFromExt("tga").? == .tga);
    try testing.expect(formatFromExt("xyz") == null);
}

test "a decode hands stb's own buffer over, leaving no block behind" {
    const a = testing.allocator;
    var pixels = [_]u8{ 9, 8, 7, 255 } ** 4;
    const enc = try encode(a, .{ .width = 2, .height = 2, .pixels = &pixels }, .png);
    defer a.free(enc);
    var dec = try decode(a, enc);
    defer dec.deinit(a);
    try testing.expect(dec.decoded); // not copied out of malloc
    try testing.expectEqual(@as(usize, 16), dec.pixels.len);
    try testing.expectEqual(@as(usize, 0), imageAlloc.stb_live); // every temporary handed back
    try testing.expect(imageAlloc.stb_owner == null);
    try testing.expectError(error.ImageDecodeFailed, decode(a, "not an image"));
    try testing.expectEqual(@as(usize, 0), imageAlloc.stb_live);
}

test "a top-down BMP decodes with its rows in stored order; its header measures positive" {
    const a = testing.allocator;
    // 1x2, 24-bit, height -2: the first stored row (red, BGR + pad) is the top one.
    const bmp = "BM\x3e\x00\x00\x00\x00\x00\x00\x00\x36\x00\x00\x00" ++
        "\x28\x00\x00\x00\x01\x00\x00\x00\xfe\xff\xff\xff\x01\x00\x18\x00" ++
        "\x00" ** 24 ++ "\x00\x00\xff\x00" ++ "\xff\x00\x00\x00";
    const d = image.dims(bmp).?;
    try testing.expectEqual(@as(usize, 1), d.width);
    try testing.expectEqual(@as(usize, 2), d.height);
    var dec = try decode(a, bmp);
    defer dec.deinit(a);
    try testing.expectEqual(@as(usize, 2), dec.height);
    try testing.expectEqualSlices(u8, &.{ 255, 0, 0, 255, 0, 0, 255, 255 }, dec.pixels);
    var zero = bmp.*;
    @memset(zero[22..26], 0); // a zero height is still refused
    try testing.expectError(error.ImageDecodeFailed, decode(a, &zero));
}

test "encode then decode round-trips dimensions and pixels" {
    const a = testing.allocator;
    var pixels = [_]u8{ 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255 };
    const img = Rgba8{ .width = 2, .height = 2, .pixels = &pixels };
    const enc = try encode(a, img, .png);
    defer a.free(enc);
    var dec = try decode(a, enc);
    defer dec.deinit(a);
    try testing.expectEqual(@as(usize, 2), dec.width);
    try testing.expectEqual(@as(usize, 2), dec.height);
    try testing.expectEqual(@as(u8, 255), dec.pixels[0]);
    try testing.expectEqual(@as(u8, 0), dec.pixels[1]);
}

test "a JPEG cut mid-scan decodes to the same pixels whatever the heap held" {
    const a = testing.allocator;
    // pystencil/tests/image/gradient420.jpg cut at 800 bytes: stb stops at a missing restart marker.
    const cut = @embedFile("../fixtures/gradient420_cut.jpg");
    var first = try decode(a, cut);
    defer first.deinit(a);
    for (0..64) |k| {
        const junk = try a.alloc(u8, 4096);
        @memset(junk, @intCast(k % 251 + 1));
        a.free(junk);
    }
    var again = try decode(a, cut);
    defer again.deinit(a);
    try testing.expectEqual(@as(usize, 37 * 23 * 4), first.pixels.len);
    try testing.expectEqualSlices(u8, first.pixels, again.pixels);
    var digest: [32]u8 = undefined;
    std.crypto.hash.sha2.Sha256.hash(first.pixels, &digest, .{});
    // pystencil's CUT_SCAN_PIXELS_SHA256 (tests/image/test_jpeg.py): one stb, both shims zero-fill.
    const want = "83396e5aa314cd2577c926d046b9a1a39ae2945e6c3bb84ea2bee5dd72fc2543";
    try testing.expectEqualStrings(want, &std.fmt.bytesToHex(digest, .lower));
}

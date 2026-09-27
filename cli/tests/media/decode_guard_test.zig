//! Hostile PNGs and BMPs through the real decode: a pixel indexing past a one-entry palette
//! decodes as opaque black whatever the stack held, an IDAT that inflates far past its header's
//! plane is refused at the block cap, never allocated, and a BMP cut short of its rows is refused.
const std = @import("std");
const image = @import("../../src/media/image.zig");
const imageAlloc = @import("../../src/media/imageAlloc.zig");
const decodeGuard = @import("../../src/media/decodeGuard.zig");
const testing = std.testing;
const Allocator = std.mem.Allocator;

fn chunk(a: Allocator, out: *std.ArrayList(u8), kind: *const [4]u8, payload: []const u8) !void {
    var len: [4]u8 = undefined;
    std.mem.writeInt(u32, &len, @intCast(payload.len), .big);
    try out.appendSlice(a, &len);
    try out.appendSlice(a, kind);
    try out.appendSlice(a, payload);
    var crc = std.hash.Crc32.init();
    crc.update(kind);
    crc.update(payload);
    std.mem.writeInt(u32, &len, crc.final(), .big);
    try out.appendSlice(a, &len);
}

fn png(a: Allocator, w: u32, h: u32, color: u8, plte: []const u8, zlib: []const u8) ![]u8 {
    var out: std.ArrayList(u8) = .empty;
    errdefer out.deinit(a);
    try out.appendSlice(a, "\x89PNG\r\n\x1a\n");
    var ihdr: [13]u8 = .{ 0, 0, 0, 0, 0, 0, 0, 0, 8, color, 0, 0, 0 };
    std.mem.writeInt(u32, ihdr[0..4], w, .big);
    std.mem.writeInt(u32, ihdr[4..8], h, .big);
    try chunk(a, &out, "IHDR", &ihdr);
    if (plte.len != 0) try chunk(a, &out, "PLTE", plte);
    try chunk(a, &out, "IDAT", zlib);
    try chunk(a, &out, "IEND", "");
    return out.toOwnedSlice(a);
}

// A zlib stream of one stored block: the raw bytes, then their Adler-32.
fn stored(a: Allocator, raw: []const u8) ![]u8 {
    var out: std.ArrayList(u8) = .empty;
    errdefer out.deinit(a);
    try out.appendSlice(a, &.{ 0x78, 0x01, 0x01 });
    var n: [4]u8 = undefined;
    std.mem.writeInt(u16, n[0..2], @intCast(raw.len), .little);
    std.mem.writeInt(u16, n[2..4], ~@as(u16, @intCast(raw.len)), .little);
    try out.appendSlice(a, &n);
    try out.appendSlice(a, raw);
    std.mem.writeInt(u32, &n, std.hash.Adler32.hash(raw), .big);
    try out.appendSlice(a, &n);
    return out.toOwnedSlice(a);
}

// A fixed-Huffman zlib stream of one zero byte and `runs` copies of it 258 long (RFC 1951
// §3.2.6): 13 bits per 258 bytes, so a small stream inflates about 160-fold.
fn zeroBomb(a: Allocator, runs: usize) ![]u8 {
    var out: std.ArrayList(u8) = .empty;
    errdefer out.deinit(a);
    try out.appendSlice(a, &.{ 0x78, 0x01 });
    var acc: u64 = 0;
    var n: u6 = 0;
    const Put = struct {
        fn bits(o: *std.ArrayList(u8), al: Allocator, ac: *u64, cnt: *u6, v: u64, k: u6) !void {
            ac.* |= v << cnt.*;
            cnt.* += k;
            while (cnt.* >= 8) : (cnt.* -= 8) {
                try o.append(al, @truncate(ac.*));
                ac.* >>= 8;
            }
        }
        fn code(o: *std.ArrayList(u8), al: Allocator, ac: *u64, cnt: *u6, c: u32, k: u5) !void {
            try bits(o, al, ac, cnt, @bitReverse(c) >> @intCast(32 - @as(u6, k)), k);
        }
    };
    try Put.bits(&out, a, &acc, &n, 0b011, 3); // final block, fixed codes
    try Put.code(&out, a, &acc, &n, 0x30, 8); // literal 0
    for (0..runs) |_| {
        try Put.code(&out, a, &acc, &n, 0xc5, 8); // length 258
        try Put.code(&out, a, &acc, &n, 0, 5); // distance 1
    }
    try Put.code(&out, a, &acc, &n, 0, 7); // end of block
    if (n != 0) try out.append(a, @truncate(acc));
    var adler: [4]u8 = undefined;
    std.mem.writeInt(u32, &adler, @as(u32, @intCast((1 + 258 * runs) % 65521)) << 16 | 1, .big);
    try out.appendSlice(a, &adler);
    return out.toOwnedSlice(a);
}

noinline fn dirtyStack() u8 {
    var junk: [64 << 10]u8 = undefined;
    @memset(std.mem.asBytes(&junk), 0xa5);
    std.mem.doNotOptimizeAway(&junk);
    return junk[junk.len - 1];
}

test "a pixel past a one-entry palette decodes as opaque black, whatever the stack held" {
    const a = testing.allocator;
    const zlib = try stored(a, &.{ 0, 0, 200, 200, 0 }); // filter None, then four indices
    defer a.free(zlib);
    const bytes = try png(a, 4, 1, 3, "\xff\x00\x00", zlib);
    defer a.free(bytes);
    for (0..3) |_| {
        _ = dirtyStack();
        var img = try image.decode(a, bytes);
        defer img.deinit(a);
        try testing.expectEqualSlices(u8, &.{ 255, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 0, 0, 255 }, img.pixels);
    }
    try testing.expectEqual(@as(usize, 0), imageAlloc.stb_live);
}

test "a BMP pixel past a one-entry palette decodes as opaque black, whatever the stack held" {
    const a = testing.allocator;
    // 4x1 8-bit, bottom-up, one palette entry (red, BGRx), indices 0 200 200 0.
    const bmp = "BM\x3e\x00\x00\x00\x00\x00\x00\x00\x3a\x00\x00\x00\x28\x00\x00\x00" ++
        "\x04\x00\x00\x00\x01\x00\x00\x00\x01\x00\x08\x00" ++ "\x00" ** 24 ++
        "\x00\x00\xff\x00" ++ "\x00\xc8\xc8\x00";
    for (0..3) |_| {
        _ = dirtyStack();
        var img = try image.decode(a, bmp);
        defer img.deinit(a);
        try testing.expectEqualSlices(u8, &.{ 255, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 0, 0, 255 }, img.pixels);
    }
    try testing.expectEqual(@as(usize, 0), imageAlloc.stb_live);
}

test "a BMP shorter than its rows is refused, though its header still measures" {
    const a = testing.allocator;
    // 2x2 24-bit, header only: stb would read the missing rows as black.
    const header = "BM\x46\x00\x00\x00\x00\x00\x00\x00\x36\x00\x00\x00\x28\x00\x00\x00" ++
        "\x02\x00\x00\x00\x02\x00\x00\x00\x01\x00\x18\x00" ++ "\x00" ** 24;
    try testing.expectError(error.ImageTruncated, image.decode(a, header));
    const d = image.dims(header).?;
    try testing.expectEqual(@as(usize, 2), d.width);
    try testing.expectEqual(@as(usize, 2), d.height);
    // Whole but for the last row's padding, it decodes: a first row of 6 bytes + 2 pad, then 6.
    const whole = header ++ "\x00\x00\xff\x00\xff\x00\x00\x00" ++ "\xff\x00\x00\x00\x00\xff";
    var img = try image.decode(a, whole);
    defer img.deinit(a);
    try testing.expectEqualSlices(u8, &.{ 0, 0, 255, 255, 255, 0, 0, 255, 255, 0, 0, 255, 0, 255, 0, 255 }, img.pixels);
    try testing.expectError(error.ImageTruncated, image.decode(a, whole[0 .. whole.len - 1]));
}

const Peak = struct {
    child: Allocator,
    most: usize = 0,

    fn allocator(self: *Peak) Allocator {
        return .{ .ptr = self, .vtable = &.{ .alloc = alloc, .resize = resize, .remap = remap, .free = free } };
    }
    fn alloc(ctx: *anyopaque, len: usize, al: std.mem.Alignment, ra: usize) ?[*]u8 {
        const self: *Peak = @ptrCast(@alignCast(ctx));
        self.most = @max(self.most, len);
        return self.child.rawAlloc(len, al, ra);
    }
    fn resize(ctx: *anyopaque, mem: []u8, al: std.mem.Alignment, len: usize, ra: usize) bool {
        const self: *Peak = @ptrCast(@alignCast(ctx));
        self.most = @max(self.most, len);
        return self.child.rawResize(mem, al, len, ra);
    }
    fn remap(ctx: *anyopaque, mem: []u8, al: std.mem.Alignment, len: usize, ra: usize) ?[*]u8 {
        const self: *Peak = @ptrCast(@alignCast(ctx));
        self.most = @max(self.most, len);
        return self.child.rawRemap(mem, al, len, ra);
    }
    fn free(ctx: *anyopaque, mem: []u8, al: std.mem.Alignment, ra: usize) void {
        const self: *Peak = @ptrCast(@alignCast(ctx));
        self.child.rawFree(mem, al, ra);
    }
};

test "an IDAT inflating far past its header's plane is refused at the cap, never allocated" {
    const a = testing.allocator;
    const zlib = try zeroBomb(a, 1 << 16); // ~16 MiB of zeros for a 1x1 RGBA image's 5 bytes
    defer a.free(zlib);
    const bytes = try png(a, 1, 1, 6, "", zlib);
    defer a.free(bytes);
    try testing.expect(bytes.len < 128 << 10);
    var peak = Peak{ .child = a };
    try testing.expectError(error.PixelDataPastHeader, image.decode(peak.allocator(), bytes));
    try testing.expect(peak.most <= decodeGuard.blockCap(bytes, 1, 1));
    try testing.expect(peak.most < 1 << 20);
    try testing.expectEqual(@as(usize, 0), imageAlloc.stb_live);
    try testing.expectEqual(@as(usize, 0), imageAlloc.stb_cap); // the cap ends with its decode
}

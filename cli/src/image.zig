//! Image codec layer — the part the C++ core deliberately doesn't do. Decodes encoded
//! bytes to a flat RGBA8 buffer and encodes an RGBA8 buffer back to a chosen format,
//! via stb_image / stb_image_write (public-domain single-header C codecs; see
//! stb_read_impl.c / stb_write_impl.c). Pure in-memory: the pipeline does the I/O.
const std = @import("std");

const c = @cImport({
    @cInclude("stb_image.h");
    @cInclude("stb_image_write.h");
});

/// A decoded image as interleaved RGBA8 (byte order R,G,B,A), owned by `allocator`.
pub const Rgba8 = struct {
    width: usize,
    height: usize,
    pixels: []u8,
    /// Set when `pixels` is the decoder's own buffer (see the stb hooks below), which is
    /// over-aligned — free it as it was allocated, never as a plain `[]u8`.
    decoded: bool = false,

    pub fn deinit(self: *Rgba8, allocator: std.mem.Allocator) void {
        const px = self.pixels;
        if (self.decoded) allocator.free(@as([]align(pixel_align) u8, @alignCast(px))) else allocator.free(px);
        self.* = undefined;
    }
};

// stb decodes through these hooks so `decode` can hand the finished pixel plane over
// instead of copying it out of malloc, which would peak at twice the image. Blocks are
// over-aligned (stb puts structs in them) and tracked per thread so free/realloc can
// rebuild the Zig slice; anything the table cannot hold falls back to libc, which decode
// then copies.
const pixel_align = 16;
const Block = struct { addr: usize, mem: []align(pixel_align) u8, owner: std.mem.Allocator };
threadlocal var stb_owner: ?std.mem.Allocator = null;
threadlocal var stb_blocks: [32]Block = undefined;
threadlocal var stb_live: usize = 0;

fn blockIndex(p: *anyopaque) ?usize {
    const addr = @intFromPtr(p);
    for (stb_blocks[0..stb_live], 0..) |b, i| if (b.addr == addr) return i;
    return null;
}

export fn stencil_stbi_alloc(n: usize) ?*anyopaque {
    const a = stb_owner orelse return std.c.malloc(n);
    if (stb_live == stb_blocks.len) return std.c.malloc(n);
    const buf = a.alignedAlloc(u8, .fromByteUnits(pixel_align), n) catch return null;
    stb_blocks[stb_live] = .{ .addr = @intFromPtr(buf.ptr), .mem = buf, .owner = a };
    stb_live += 1;
    return buf.ptr;
}

export fn stencil_stbi_realloc(p: ?*anyopaque, n: usize) ?*anyopaque {
    const raw = p orelse return stencil_stbi_alloc(n);
    const i = blockIndex(raw) orelse return std.c.realloc(raw, n);
    // A failed grow leaves the old block valid and still tracked, as realloc promises.
    const grown = stb_blocks[i].owner.realloc(stb_blocks[i].mem, n) catch return null;
    stb_blocks[i] = .{ .addr = @intFromPtr(grown.ptr), .mem = grown, .owner = stb_blocks[i].owner };
    return grown.ptr;
}

export fn stencil_stbi_free(p: ?*anyopaque) void {
    const raw = p orelse return;
    const i = blockIndex(raw) orelse return std.c.free(raw);
    const b = stb_blocks[i];
    stb_blocks[i] = stb_blocks[stb_live - 1];
    stb_live -= 1;
    b.owner.free(b.mem);
}

/// Take a decoded block out of the table: the caller owns it from here, so stbi_image_free
/// must not be called on it. null when stb allocated it through the libc fallback.
fn takeBlock(p: *anyopaque) ?[]align(pixel_align) u8 {
    const i = blockIndex(p) orelse return null;
    const mem = stb_blocks[i].mem;
    stb_blocks[i] = stb_blocks[stb_live - 1];
    stb_live -= 1;
    return mem;
}

/// Output container formats the CLI can encode (the formats stb can write).
pub const Format = enum {
    png,
    jpeg,
    bmp,
    tga,

    /// Canonical file extension (no dot).
    pub fn ext(self: Format) []const u8 {
        return switch (self) {
            .png => "png",
            .jpeg => "jpg",
            .bmp => "bmp",
            .tga => "tga",
        };
    }
};

/// Map a file extension (with or without dot, any case) to an output format.
pub fn formatFromExt(ext: []const u8) ?Format {
    var buf: [8]u8 = undefined;
    const e = if (ext.len > 0 and ext[0] == '.') ext[1..] else ext;
    if (e.len == 0 or e.len > buf.len) return null;
    const low = std.ascii.lowerString(buf[0..e.len], e);
    if (std.mem.eql(u8, low, "png")) return .png;
    if (std.mem.eql(u8, low, "jpg") or std.mem.eql(u8, low, "jpeg")) return .jpeg;
    if (std.mem.eql(u8, low, "bmp")) return .bmp;
    if (std.mem.eql(u8, low, "tga")) return .tga;
    return null;
}

/// Pixel-area cap for a decoded image. `w*h*4` must fit a c_int (the stride/size arguments
/// handed back to stb when encoding), which caps the area at 2^29; 2^28 keeps a decoded
/// buffer under 1 GiB. Matches the per-side STBI_MAX_DIMENSIONS 16384 in stb_read_impl.c.
pub const max_pixels: usize = 16384 * 16384;

/// Header-only size probe: no pixel plane is ever allocated. Null when stb cannot read the
/// header or the dimensions are past `max_pixels`.
pub fn dims(bytes: []const u8) ?struct { width: usize, height: usize } {
    var w: c_int = 0;
    var h: c_int = 0;
    var channels: c_int = 0;
    if (c.stbi_info_from_memory(bytes.ptr, @intCast(bytes.len), &w, &h, &channels) == 0) return null;
    if (w <= 0 or h <= 0) return null;
    const width: usize = @intCast(w);
    const height: usize = @intCast(h);
    if (width * height > max_pixels) return null;
    return .{ .width = width, .height = height };
}

/// Decode encoded image bytes into an owned RGBA8 buffer.
pub fn decode(allocator: std.mem.Allocator, bytes: []const u8) !Rgba8 {
    var w: c_int = 0;
    var h: c_int = 0;
    var channels: c_int = 0;
    // Read the header first: a crafted w/h is refused before stb allocates w*h*4 bytes.
    if (c.stbi_info_from_memory(bytes.ptr, @intCast(bytes.len), &w, &h, &channels) != 0) {
        if (w <= 0 or h <= 0) return error.ImageDecodeFailed;
        if (@as(usize, @intCast(w)) * @as(usize, @intCast(h)) > max_pixels) return error.ImageTooLarge;
    }
    const prev_owner = stb_owner;
    stb_owner = allocator;
    defer stb_owner = prev_owner;

    const data = c.stbi_load_from_memory(bytes.ptr, @intCast(bytes.len), &w, &h, &channels, 4);
    if (data == null) return error.ImageDecodeFailed;
    if (w <= 0 or h <= 0 or @as(usize, @intCast(w)) * @as(usize, @intCast(h)) > max_pixels) {
        c.stbi_image_free(data);
        return if (w <= 0 or h <= 0) error.ImageDecodeFailed else error.ImageTooLarge;
    }

    const n = @as(usize, @intCast(w)) * @as(usize, @intCast(h)) * 4;
    const block = takeBlock(data.?) orelse {
        defer c.stbi_image_free(data);
        return .{ .width = @intCast(w), .height = @intCast(h), .pixels = try allocator.dupe(u8, data[0..n]) };
    };
    // The decode already ran on this allocator: trim the block to the exact pixel count
    // (stb's own is sometimes a byte longer) and hand it over uncopied.
    const pixels = allocator.realloc(block, n) catch {
        defer allocator.free(block);
        return .{ .width = @intCast(w), .height = @intCast(h), .pixels = try allocator.dupe(u8, block[0..n]) };
    };
    return .{ .width = @intCast(w), .height = @intCast(h), .pixels = pixels, .decoded = true };
}

// stb hands encoded bytes to this callback in chunks; we accumulate them.
const WriteCtx = struct {
    list: *std.ArrayList(u8),
    allocator: std.mem.Allocator,
    failed: bool = false,
};

fn writeCb(context: ?*anyopaque, data: ?*anyopaque, size: c_int) callconv(.c) void {
    const ctx: *WriteCtx = @ptrCast(@alignCast(context.?));
    if (ctx.failed or size <= 0 or data == null) return;
    const bytes: [*]const u8 = @ptrCast(data.?);
    ctx.list.appendSlice(ctx.allocator, bytes[0..@intCast(size)]) catch {
        ctx.failed = true;
    };
}

/// Encode an RGBA8 buffer to `fmt`, returning owned encoded bytes.
pub fn encode(allocator: std.mem.Allocator, img: Rgba8, fmt: Format) ![]u8 {
    var list: std.ArrayList(u8) = .empty;
    errdefer list.deinit(allocator);
    var ctx = WriteCtx{ .list = &list, .allocator = allocator };

    const w: c_int = @intCast(img.width);
    const h: c_int = @intCast(img.height);
    const stride: c_int = @intCast(img.width * 4);
    const px = img.pixels.ptr;

    // comp = 4 (RGBA); png/tga keep alpha, jpg/bmp drop it (stb ignores it).
    const rc = switch (fmt) {
        .png => c.stbi_write_png_to_func(writeCb, &ctx, w, h, 4, px, stride),
        .jpeg => c.stbi_write_jpg_to_func(writeCb, &ctx, w, h, 4, px, 90),
        .bmp => c.stbi_write_bmp_to_func(writeCb, &ctx, w, h, 4, px),
        .tga => c.stbi_write_tga_to_func(writeCb, &ctx, w, h, 4, px),
    };
    if (rc == 0 or ctx.failed) return error.ImageEncodeFailed;
    return list.toOwnedSlice(allocator);
}

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
    try testing.expectEqual(@as(usize, 0), stb_live); // every temporary handed back
    try testing.expect(stb_owner == null);
    try testing.expectError(error.ImageDecodeFailed, decode(a, "not an image"));
    try testing.expectEqual(@as(usize, 0), stb_live);
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

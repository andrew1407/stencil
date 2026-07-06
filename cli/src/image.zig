//! Image codec layer — the part the C++ core deliberately doesn't do. Decodes encoded
//! bytes to a flat RGBA8 buffer and encodes an RGBA8 buffer back to a chosen format,
//! via stb_image / stb_image_write (public-domain single-header C codecs; see
//! stb_impl.c). Pure in-memory: the pipeline handles file/URL/stdout I/O.
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

    pub fn deinit(self: *Rgba8, allocator: std.mem.Allocator) void {
        allocator.free(self.pixels);
        self.* = undefined;
    }
};

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

/// Decode encoded image bytes into an owned RGBA8 buffer.
pub fn decode(allocator: std.mem.Allocator, bytes: []const u8) !Rgba8 {
    var w: c_int = 0;
    var h: c_int = 0;
    var channels: c_int = 0;
    const data = c.stbi_load_from_memory(bytes.ptr, @intCast(bytes.len), &w, &h, &channels, 4);
    if (data == null) return error.ImageDecodeFailed;
    defer c.stbi_image_free(data);
    if (w <= 0 or h <= 0) return error.ImageDecodeFailed;

    const n = @as(usize, @intCast(w)) * @as(usize, @intCast(h)) * 4;
    return .{
        .width = @intCast(w),
        .height = @intCast(h),
        .pixels = try allocator.dupe(u8, data[0..n]),
    };
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

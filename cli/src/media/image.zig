//! Image codec layer — the part the C++ core deliberately doesn't do. Decodes encoded
//! bytes to a flat RGBA8 buffer and encodes an RGBA8 buffer back to a chosen format,
//! via stb_image / stb_image_write (public-domain single-header C codecs; see
//! stb_read_impl.c / stb_write_impl.c). Pure in-memory: the pipeline does the I/O.
const std = @import("std");

const c = @cImport({
    @cInclude("stb_image.h");
    @cInclude("stb_image_write.h");
});
const imageAlloc = @import("imageAlloc.zig");
const decodeGuard = @import("decodeGuard.zig");

/// A decoded image as interleaved RGBA8 (byte order R,G,B,A), owned by `allocator`.
pub const Rgba8 = struct {
    width: usize,
    height: usize,
    pixels: []u8,
    /// Set when `pixels` is the decoder's own buffer (see imageAlloc.zig's stb hooks), which is
    /// over-aligned — free it as it was allocated, never as a plain `[]u8`.
    decoded: bool = false,

    pub fn deinit(self: *Rgba8, allocator: std.mem.Allocator) void {
        const px = self.pixels;
        if (self.decoded) allocator.free(@as([]align(imageAlloc.pixel_align) u8, @alignCast(px))) else allocator.free(px);
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

/// The extension of the LAST path segment without the dot, else null, so a dot in a directory name is
/// not mistaken for one. `path` is literal: a `?query` is part of the name, as an output path needs.
pub fn extOf(path: []const u8) ?[]const u8 {
    const dot = std.mem.lastIndexOfScalar(u8, path, '.') orelse return null;
    if (std.mem.lastIndexOfAny(u8, path, "/\\")) |s| if (dot < s) return null;
    if (dot + 1 >= path.len) return null;
    return path[dot + 1 ..];
}

/// The format a SOURCE path or URL names, or null. Unlike `extOf` this trims a `?query` /
/// `#fragment` first, so `a.jpg?v=2` is a jpeg.
pub fn formatOfPath(path: []const u8) ?Format {
    var end = path.len;
    if (std.mem.indexOfAny(u8, path, "?#")) |q| end = q;
    return formatFromExt(extOf(path[0..end]) orelse return null);
}

/// Pixel-area cap for a decoded image: `w*h*4` must fit a c_int (stb's stride/size arguments), which
/// caps the area at 2^29; 2^28 keeps a buffer under 1 GiB. Matches STBI_MAX_DIMENSIONS 16384.
pub const max_pixels: usize = 16384 * 16384;

/// stb's header read, with a top-down BMP's height as its magnitude: stbi_info reports the
/// stored negative height, while stb's load reads that sign as the row order and flips it.
fn info(bytes: []const u8, w: *c_int, h: *c_int, channels: *c_int) bool {
    if (c.stbi_info_from_memory(bytes.ptr, @intCast(bytes.len), w, h, channels) == 0) return false;
    if (h.* < 0 and h.* != std.math.minInt(c_int) and std.mem.startsWith(u8, bytes, "BM")) h.* = -h.*;
    return true;
}

/// Header-only size probe: no pixel plane is ever allocated. Null when stb cannot read the
/// header or the dimensions are past `max_pixels`.
pub fn dims(bytes: []const u8) ?struct { width: usize, height: usize } {
    var w: c_int = 0;
    var h: c_int = 0;
    var channels: c_int = 0;
    if (!info(bytes, &w, &h, &channels)) return null;
    if (w <= 0 or h <= 0) return null;
    const width: usize = @intCast(w);
    const height: usize = @intCast(h);
    if (width * height > max_pixels) return null;
    return .{ .width = width, .height = height };
}

/// Decode encoded image bytes into an owned RGBA8 buffer. `PixelDataPastHeader`: the data asked
/// stb for a block past the largest plane its header allows (a PNG inflating without end);
/// `ImageTruncated`: a BMP's rows run past the end of the file.
pub fn decode(allocator: std.mem.Allocator, encoded: []const u8) !Rgba8 {
    const padded = try decodeGuard.prepared(allocator, encoded);
    defer if (padded) |p| allocator.free(p);
    const bytes = padded orelse encoded;
    var w: c_int = 0;
    var h: c_int = 0;
    var channels: c_int = 0;
    // Read the header first: a crafted w/h is refused before stb allocates w*h*4 bytes.
    if (info(bytes, &w, &h, &channels)) {
        if (w <= 0 or h <= 0) return error.ImageDecodeFailed;
        if (@as(usize, @intCast(w)) * @as(usize, @intCast(h)) > max_pixels) return error.ImageTooLarge;
    } else {
        w = 0;
        h = 0;
    }
    const prev_owner = imageAlloc.stb_owner;
    imageAlloc.stb_owner = allocator;
    defer imageAlloc.stb_owner = prev_owner;
    imageAlloc.stb_cap = decodeGuard.blockCap(bytes, @intCast(w), @intCast(h));
    imageAlloc.stb_cap_hit = false;
    defer imageAlloc.stb_cap = 0;

    const data = c.stbi_load_from_memory(bytes.ptr, @intCast(bytes.len), &w, &h, &channels, 4);
    if (data == null) return if (imageAlloc.stb_cap_hit) error.PixelDataPastHeader else error.ImageDecodeFailed;
    if (w <= 0 or h <= 0 or @as(usize, @intCast(w)) * @as(usize, @intCast(h)) > max_pixels) {
        c.stbi_image_free(data);
        return if (w <= 0 or h <= 0) error.ImageDecodeFailed else error.ImageTooLarge;
    }

    const n = @as(usize, @intCast(w)) * @as(usize, @intCast(h)) * 4;
    const block = imageAlloc.takeBlock(data.?) orelse {
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

test {
    _ = imageAlloc;
    _ = decodeGuard;
}

//! The allocator hooks stb_read_impl.c routes STBI_MALLOC/REALLOC/FREE through. A decode's blocks
//! come from its own allocator, over-aligned and tracked per thread, so `image.decode` hands the
//! pixel plane over uncopied; every byte stb is given starts at zero, so a scan cut short decodes
//! the same whatever the heap held — as pystencil/stb/shim.c's hooks do.
const std = @import("std");

pub const pixel_align = 16;
const Block = struct { addr: usize, mem: []align(pixel_align) u8, owner: std.mem.Allocator };
pub threadlocal var stb_owner: ?std.mem.Allocator = null;
threadlocal var stb_blocks: [32]Block = undefined;
pub threadlocal var stb_live: usize = 0;
/// The largest block the running decode may take (0 = no cap), and whether one asked for more.
pub threadlocal var stb_cap: usize = 0;
pub threadlocal var stb_cap_hit: bool = false;

fn overCap(n: usize) bool {
    if (stb_cap == 0 or n <= stb_cap) return false;
    stb_cap_hit = true;
    return true;
}

/// A libc block keeps its size in front of it, so a grow knows which bytes are new.
const header = pixel_align;

fn blockIndex(p: *anyopaque) ?usize {
    const addr = @intFromPtr(p);
    for (stb_blocks[0..stb_live], 0..) |b, i| if (b.addr == addr) return i;
    return null;
}

fn libcAlloc(n: usize) ?*anyopaque {
    const base: [*]u8 = @ptrCast(std.c.calloc(1, n + header) orelse return null);
    std.mem.writeInt(usize, base[0..@sizeOf(usize)], n, .native);
    return base + header;
}

fn libcRealloc(p: *anyopaque, n: usize) ?*anyopaque {
    const old_base = @as([*]u8, @ptrCast(p)) - header;
    const old = std.mem.readInt(usize, old_base[0..@sizeOf(usize)], .native);
    const base: [*]u8 = @ptrCast(std.c.realloc(old_base, n + header) orelse return null);
    if (n > old) @memset(base[header + old .. header + n], 0);
    std.mem.writeInt(usize, base[0..@sizeOf(usize)], n, .native);
    return base + header;
}

export fn stencil_stbi_alloc(n: usize) ?*anyopaque {
    if (overCap(n)) return null;
    const a = stb_owner orelse return libcAlloc(n);
    if (stb_live == stb_blocks.len) return libcAlloc(n);
    const buf = a.alignedAlloc(u8, .fromByteUnits(pixel_align), n) catch return null;
    @memset(buf, 0);
    stb_blocks[stb_live] = .{ .addr = @intFromPtr(buf.ptr), .mem = buf, .owner = a };
    stb_live += 1;
    return buf.ptr;
}

export fn stencil_stbi_realloc(p: ?*anyopaque, n: usize) ?*anyopaque {
    const raw = p orelse return stencil_stbi_alloc(n);
    if (overCap(n)) return null;
    const i = blockIndex(raw) orelse return libcRealloc(raw, n);
    const old = stb_blocks[i].mem.len;
    // A failed grow leaves the old block valid and still tracked, as realloc promises.
    const grown = stb_blocks[i].owner.realloc(stb_blocks[i].mem, n) catch return null;
    if (n > old) @memset(grown[old..], 0);
    stb_blocks[i] = .{ .addr = @intFromPtr(grown.ptr), .mem = grown, .owner = stb_blocks[i].owner };
    return grown.ptr;
}

export fn stencil_stbi_free(p: ?*anyopaque) void {
    const raw = p orelse return;
    const i = blockIndex(raw) orelse return std.c.free(@as([*]u8, @ptrCast(raw)) - header);
    const b = stb_blocks[i];
    stb_blocks[i] = stb_blocks[stb_live - 1];
    stb_live -= 1;
    b.owner.free(b.mem);
}

/// Take a decoded block out of the table: the caller owns it from here, so stbi_image_free
/// must not be called on it. null when stb allocated it through the libc fallback.
pub fn takeBlock(p: *anyopaque) ?[]align(pixel_align) u8 {
    const i = blockIndex(p) orelse return null;
    const mem = stb_blocks[i].mem;
    stb_blocks[i] = stb_blocks[stb_live - 1];
    stb_live -= 1;
    return mem;
}

test "every block stb is handed starts at zero, and so does every byte a grow adds" {
    const gpa = std.testing.allocator;
    stb_owner = gpa;
    defer stb_owner = null;
    const p: [*]u8 = @ptrCast(stencil_stbi_alloc(64).?);
    try std.testing.expect(std.mem.allEqual(u8, p[0..64], 0));
    @memset(p[0..64], 0xee);
    const q: [*]u8 = @ptrCast(stencil_stbi_realloc(p, 4096).?);
    try std.testing.expect(std.mem.allEqual(u8, q[64..4096], 0));
    stencil_stbi_free(q);

    stb_owner = null; // the libc fallback, the same promise
    const r: [*]u8 = @ptrCast(stencil_stbi_alloc(8).?);
    @memset(r[0..8], 0xee);
    const s: [*]u8 = @ptrCast(stencil_stbi_realloc(r, 4096).?);
    try std.testing.expect(std.mem.allEqual(u8, s[8..4096], 0));
    stencil_stbi_free(s);
    try std.testing.expectEqual(@as(usize, 0), stb_live);
}

test "a capped decode is refused any block past the cap, and a grow past it keeps the old block" {
    stb_cap = 4096;
    stb_cap_hit = false;
    defer stb_cap = 0;
    const p = stencil_stbi_alloc(4096).?;
    try std.testing.expect(!stb_cap_hit);
    try std.testing.expect(stencil_stbi_realloc(p, 4097) == null);
    try std.testing.expect(stencil_stbi_alloc(1 << 20) == null);
    try std.testing.expect(stb_cap_hit);
    stencil_stbi_free(p);
}

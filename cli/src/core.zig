//! Typed Zig wrappers over the shared C++ core's extern "C" ABI (../core/cliApi.h).
//! The core owns all geometry / colour / length / raster logic; this file is a thin,
//! allocation-aware bridge. The C strings it needs are produced with dupeZ so callers
//! can pass ordinary Zig slices.
const std = @import("std");

const c = @cImport({
    @cInclude("core/cliApi.h");
});

pub const Rgba = struct { r: u8, g: u8, b: u8, a: u8 };
pub const Rect = struct { x: i32, y: i32, w: i32, h: i32 };
pub const Size = struct { w: i32, h: i32 };
pub const Page = struct { w: f64, h: f64 };

fn toByte(v: c_int) u8 {
    return @intCast(std.math.clamp(v, 0, 255));
}

/// Parse a CSS colour (named / hex / "transparent"). null if unrecognized.
pub fn parseColor(allocator: std.mem.Allocator, spec: []const u8) ?Rgba {
    const z = allocator.dupeZ(u8, spec) catch return null;
    defer allocator.free(z);
    var r: c_int = 0;
    var g: c_int = 0;
    var b: c_int = 0;
    var a: c_int = 0;
    if (c.stencil_cli_parseColor(z.ptr, &r, &g, &b, &a) == 0) return null;
    return .{ .r = toByte(r), .g = toByte(g), .b = toByte(b), .a = toByte(a) };
}

/// Named page size in cm (e.g. "A4"). null if unknown.
pub fn namedPageSize(allocator: std.mem.Allocator, name: []const u8) ?Page {
    const z = allocator.dupeZ(u8, name) catch return null;
    defer allocator.free(z);
    var w: f64 = 0;
    var h: f64 = 0;
    if (c.stencil_cli_namedPageSize(z.ptr, &w, &h) == 0) return null;
    return .{ .w = w, .h = h };
}

pub fn defaultBlankSizePx(page_w_cm: f64, page_h_cm: f64, dpi: f64) Size {
    var w: c_int = 0;
    var h: c_int = 0;
    c.stencil_cli_defaultBlankSizePx(page_w_cm, page_h_cm, dpi, &w, &h);
    return .{ .w = @intCast(w), .h = @intCast(h) };
}

/// Resolve a crop spec string to a clamped integer pixel rect. null on a bad spec.
pub fn resolveCrop(
    allocator: std.mem.Allocator,
    spec: []const u8,
    image_w: f64,
    image_h: f64,
    px_per_cm_x: f64,
    px_per_cm_y: f64,
    page_w_cm: f64,
    page_h_cm: f64,
    album: bool,
) ?Rect {
    const z = allocator.dupeZ(u8, spec) catch return null;
    defer allocator.free(z);
    var x: c_int = 0;
    var y: c_int = 0;
    var w: c_int = 0;
    var h: c_int = 0;
    const ok = c.stencil_cli_resolveCrop(z.ptr, image_w, image_h, px_per_cm_x,
        px_per_cm_y, page_w_cm, page_h_cm, @intFromBool(album), &x, &y, &w, &h);
    if (ok == 0) return null;
    return .{ .x = @intCast(x), .y = @intCast(y), .w = @intCast(w), .h = @intCast(h) };
}

pub fn cropImageRGBA(src: []const u8, src_w: i32, src_h: i32, rect: Rect, dst: []u8) void {
    c.stencil_cli_cropImageRGBA(src.ptr, src_w, src_h, rect.x, rect.y, rect.w, rect.h, dst.ptr);
}

pub fn normalizeQuarters(q: i32) i32 {
    return c.stencil_cli_normalizeQuarters(q);
}

pub fn rotatedDims(w: i32, h: i32, quarters: i32) Size {
    var ow: c_int = 0;
    var oh: c_int = 0;
    c.stencil_cli_rotatedDims(w, h, quarters, &ow, &oh);
    return .{ .w = @intCast(ow), .h = @intCast(oh) };
}

pub fn rotateImageRGBA(src: []const u8, w: i32, h: i32, quarters: i32, dst: []u8) void {
    c.stencil_cli_rotateImageRGBA(src.ptr, w, h, quarters, dst.ptr);
}

pub fn fillRGBA(dst: []u8, pixel_count: i32, color: Rgba) void {
    c.stencil_cli_fillRGBA(dst.ptr, pixel_count, color.r, color.g, color.b, color.a);
}

/// Apply an image filter in place. `mode` is "bw"|"sepia"|"none"|a custom colour.
pub fn applyFilter(allocator: std.mem.Allocator, mode: []const u8, data: []u8, pixel_count: i32, tint: Rgba) void {
    const z = allocator.dupeZ(u8, mode) catch return;
    defer allocator.free(z);
    c.stencil_cli_applyFilter(z.ptr, data.ptr, pixel_count, tint.r, tint.g, tint.b);
}

/// One layout line, ready to rasterise. Strings are null-terminated for the C ABI.
pub const LineDraw = struct {
    points: []const f64, // x,y pairs (2 * n_points)
    color: [:0]const u8,
    thickness: f64,
    marker_size: f64,
    style: [:0]const u8,
    locked: bool,
    fill_color: [:0]const u8,
};

pub fn rasterizeLine(buf: []u8, w: i32, h: i32, line: LineDraw) void {
    const n_pts: c_int = @intCast(line.points.len / 2);
    c.stencil_cli_rasterizeLine(buf.ptr, w, h, line.points.ptr, n_pts, line.color.ptr,
        line.thickness, line.marker_size, line.style.ptr, @intFromBool(line.locked),
        line.fill_color.ptr);
}

/// Validate a single-variable formula (`var_name` is 'x' or 'y'). Empty = valid (identity).
pub fn validateFormula(allocator: std.mem.Allocator, expr: []const u8, var_name: u8) bool {
    const z = allocator.dupeZ(u8, expr) catch return false;
    defer allocator.free(z);
    return c.stencil_cli_validateFormula(z.ptr, @as(c_int, var_name)) != 0;
}

/// Apply a formula to `value` ('x'/'y' variable). Identity when disabled, empty, or invalid.
pub fn applyFormula(allocator: std.mem.Allocator, expr: []const u8, var_name: u8, value: f64, allow: bool) f64 {
    const z = allocator.dupeZ(u8, expr) catch return value;
    defer allocator.free(z);
    return c.stencil_cli_applyFormula(z.ptr, @as(c_int, var_name), value, @intFromBool(allow));
}

const testing = std.testing;

test "parseColor: names, hex, rejects junk" {
    const a = testing.allocator;
    const red = parseColor(a, "red").?;
    try testing.expectEqual(@as(u8, 255), red.r);
    try testing.expectEqual(@as(u8, 0), red.g);
    try testing.expect(parseColor(a, "#0000ff").?.b == 255);
    try testing.expect(parseColor(a, "notacolour") == null);
}

test "resolveCrop + rotate helpers" {
    const a = testing.allocator;
    const rect = resolveCrop(a, "x1=0px x2=100px y1=0px y2=50px", 200, 200, 10, 10, 21, 29.7, false).?;
    try testing.expectEqual(@as(i32, 100), rect.w);
    try testing.expectEqual(@as(i32, 50), rect.h);
    try testing.expect(resolveCrop(a, "z=1", 200, 200, 10, 10, 21, 29.7, false) == null);
    try testing.expectEqual(@as(i32, 3), normalizeQuarters(-1));
    const d = rotatedDims(4, 2, 1);
    try testing.expect(d.w == 2 and d.h == 4);
}

test "formula validate + apply through the ABI" {
    const a = testing.allocator;
    try testing.expect(validateFormula(a, "x*2", 'x'));
    try testing.expect(validateFormula(a, "", 'x')); // empty = identity = valid
    try testing.expect(!validateFormula(a, "foo(x)", 'x')); // unknown ident = invalid
    try testing.expectEqual(@as(f64, 20), applyFormula(a, "x*2", 'x', 10, true));
    try testing.expectEqual(@as(f64, 10), applyFormula(a, "x*2", 'x', 10, false)); // disabled = identity
    try testing.expectEqual(@as(f64, 10), applyFormula(a, "bad(", 'x', 10, true)); // invalid = identity
}

test "namedPageSize + blank fill round trips through the ABI" {
    const a = testing.allocator;
    const p = namedPageSize(a, "A4").?;
    try testing.expectApproxEqAbs(@as(f64, 21.0), p.w, 0.01);
    var px = [_]u8{0} ** 8;
    fillRGBA(&px, 2, .{ .r = 10, .g = 20, .b = 30, .a = 40 });
    try testing.expectEqual(@as(u8, 10), px[0]);
    try testing.expectEqual(@as(u8, 40), px[7]);
}

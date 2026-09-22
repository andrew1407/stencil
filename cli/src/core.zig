//! Typed Zig wrappers over the shared C++ core's extern "C" ABI (../core/cliApi.h).
//! The core owns all geometry / colour / length / raster logic; this file is a thin,
//! allocation-free bridge: every string argument is `[:0]const u8`, so the caller owns
//! the NUL (a literal already has one) instead of the wrapper duping one per call.
//! The row-range slice of the same ABI lives in imageRows.zig, its only consumer.
const std = @import("std");
const formula = @import("core/formula.zig");

const c = @cImport({
    @cInclude("core/cliApi.h");
});

pub const Rgba = struct { r: u8, g: u8, b: u8, a: u8 };
pub const Rect = struct { x: i32, y: i32, w: i32, h: i32 };
pub const Size = struct { w: i32, h: i32 };
pub const Page = struct { w: f64, h: f64 };

/// A NUL-terminated copy of `s` in this thread's scratch, for a caller whose string came from a
/// console line or a model plan. Valid until this thread's next call; null when it does not fit.
pub fn zstr(s: []const u8) ?[:0]const u8 {
    const S = struct {
        threadlocal var buf: [4096]u8 = undefined; // a console line is at most this
    };
    if (s.len >= S.buf.len) return null;
    @memcpy(S.buf[0..s.len], s);
    S.buf[s.len] = 0;
    return S.buf[0..s.len :0];
}

fn toByte(v: c_int) u8 {
    return @intCast(std.math.clamp(v, 0, 255));
}

/// Parse a CSS colour (named / hex / "transparent"). null if unrecognized.
pub fn parseColor(spec: [:0]const u8) ?Rgba {
    var r: c_int = 0;
    var g: c_int = 0;
    var b: c_int = 0;
    var a: c_int = 0;
    if (c.stencil_cli_parseColor(spec.ptr, &r, &g, &b, &a) == 0) return null;
    return .{ .r = toByte(r), .g = toByte(g), .b = toByte(b), .a = toByte(a) };
}

/// The canonical page-format names ("A0 A1 … C10"), space-separated, in canonical order.
/// A static string owned by the core — no allocation, valid for the program's lifetime.
pub fn pageFormats() []const u8 {
    return std.mem.span(c.stencil_cli_pageFormats());
}

/// Resolve a page-format name case-insensitively to its canonical spelling ("b5" -> "B5"); null when
/// unknown ("custom" included). The result slices into the core's static list, so it never dangles.
pub fn canonicalPageFormat(name: []const u8) ?[]const u8 {
    // The list is a C pointer, so it cannot be a comptime map; the length test skips the
    // case-insensitive compare for all but the two or three names of the right width.
    if (name.len == 0 or name.len > 4) return null;
    var it = std.mem.tokenizeScalar(u8, pageFormats(), ' ');
    while (it.next()) |n| {
        if (n.len == name.len and std.ascii.eqlIgnoreCase(n, name)) return n;
    }
    return null;
}

/// Named page size in cm (e.g. "A4"). null if unknown.
pub fn namedPageSize(name: [:0]const u8) ?Page {
    var w: f64 = 0;
    var h: f64 = 0;
    if (c.stencil_cli_namedPageSize(name.ptr, &w, &h) == 0) return null;
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
    spec: [:0]const u8,
    image_w: f64,
    image_h: f64,
    px_per_cm_x: f64,
    px_per_cm_y: f64,
    page_w_cm: f64,
    page_h_cm: f64,
    album: bool,
) ?Rect {
    var x: c_int = 0;
    var y: c_int = 0;
    var w: c_int = 0;
    var h: c_int = 0;
    const ok = c.stencil_cli_resolveCrop(spec.ptr, image_w, image_h, px_per_cm_x,
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

/// Apply an image filter in place. `mode` is "bw"|"sepia"|"invert"|"none"|a custom colour.
pub fn applyFilter(mode: [:0]const u8, data: []u8, pixel_count: i32, tint: Rgba) void {
    c.stencil_cli_applyFilter(mode.ptr, data.ptr, pixel_count, tint.r, tint.g, tint.b);
}

/// Sobel contour (edge-detection) filter in place — dark edges on white, alpha preserved.
/// Unlike applyFilter it needs the image dimensions (it reads pixel neighbourhoods).
pub fn applyContour(data: []u8, width: i32, height: i32) void {
    c.stencil_cli_applyContour(data.ptr, width, height);
}

/// One layout line, ready to rasterise. Strings are null-terminated for the C ABI.
pub const LineDraw = struct {
    points: []const f64, // x,y pairs (2 * n_points)
    color: [:0]const u8,
    thickness: f64,
    point_size: f64,
    style: [:0]const u8,
    locked: bool,
    fill_color: [:0]const u8,
    /// Point colour. Empty = inherit `color`.
    point_color: [:0]const u8 = "",
};

pub fn rasterizeLine(buf: []u8, w: i32, h: i32, line: LineDraw) void {
    const n_pts: c_int = @intCast(line.points.len / 2);
    c.stencil_cli_rasterizeLine(buf.ptr, w, h, line.points.ptr, n_pts, line.color.ptr,
        line.thickness, line.point_size, line.style.ptr, @intFromBool(line.locked),
        line.fill_color.ptr, line.point_color.ptr);
}

// The formula bridge lives in core/formula.zig; re-exported so every caller goes through core.
pub const FormulaCtx = formula.FormulaCtx;
pub const validateFormula = formula.validateFormula;
pub const applyFormula = formula.applyFormula;
pub const validateFormulaCtx = formula.validateFormulaCtx;
pub const applyFormulaCtx = formula.applyFormulaCtx;

/// Parse a human duration ("days 23", "fortnight", "month", "off") into ms (0 for off/never), else
/// null. The caller adds it to "now" to get an expiry timestamp.
pub fn parseDuration(spec: [:0]const u8) ?i64 {
    var ms: c_longlong = 0;
    if (c.stencil_cli_parseDuration(spec.ptr, &ms) == 0) return null;
    return @intCast(ms);
}

/// Number of CSS colour keywords the core's own table carries.
pub fn colorNameCount() usize {
    return @intCast(c.stencil_cli_colorNameCount());
}

/// The colour keyword at `index` (alphabetical) and its 0xRRGGBB. null out of range.
/// Together with colorNameCount this makes a table drift check bidirectional.
pub fn colorNameAt(index: usize) ?struct { name: [:0]const u8, rgb: u32 } {
    var rgb: c_uint = 0;
    const p = c.stencil_cli_colorNameAt(@intCast(index), &rgb) orelse return null;
    return .{ .name = std.mem.span(p), .rgb = @intCast(rgb) };
}

/// The `/expire` unit words and keep-forever aliases the core parser accepts, joined as help text.
/// Backed by a call-local static buffer: both lists are short and fixed, and the console prints at once.
pub fn durationUnitsHelp() []const u8 {
    const S = struct {
        var buf: [96]u8 = undefined;
    };
    return joinPipes(&S.buf, std.mem.span(c.stencil_cli_durationUnits()));
}

pub fn durationOffHelp() []const u8 {
    const S = struct {
        var buf: [48]u8 = undefined;
    };
    return joinPipes(&S.buf, std.mem.span(c.stencil_cli_durationOffAliases()));
}

fn joinPipes(buf: []u8, words: []const u8) []const u8 {
    var n: usize = 0;
    var it = std.mem.tokenizeScalar(u8, words, ' ');
    while (it.next()) |word| {
        if (n > 0) {
            @memcpy(buf[n..][0..3], " | ");
            n += 3;
        }
        @memcpy(buf[n..][0..word.len], word);
        n += word.len;
    }
    return buf[0..n];
}

const testing = std.testing;

test "parseColor: names, hex, rejects junk" {
    const red = parseColor("red").?;
    try testing.expectEqual(@as(u8, 255), red.r);
    try testing.expectEqual(@as(u8, 0), red.g);
    try testing.expect(parseColor("#0000ff").?.b == 255);
    try testing.expect(parseColor("notacolour") == null);
}

test "resolveCrop + rotate helpers" {
    const rect = resolveCrop("x1=0px x2=100px y1=0px y2=50px", 200, 200, 10, 10, 21, 29.7, false).?;
    try testing.expectEqual(@as(i32, 100), rect.w);
    try testing.expectEqual(@as(i32, 50), rect.h);
    try testing.expect(resolveCrop("z=1", 200, 200, 10, 10, 21, 29.7, false) == null);
    try testing.expectEqual(@as(i32, 3), normalizeQuarters(-1));
    const d = rotatedDims(4, 2, 1);
    try testing.expect(d.w == 2 and d.h == 4);
}

test {
    _ = formula;
}

test "the core's expire vocabulary joins into the console help lists" {
    try testing.expectEqualStrings("day | week | fortnight | month | year", durationUnitsHelp());
    try testing.expectEqualStrings("off | never | none", durationOffHelp());
}

test "colour-name enumeration is alphabetical and matches parseColor" {
    try testing.expect(colorNameCount() > 100);
    const first = colorNameAt(0).?;
    try testing.expectEqualStrings("aliceblue", first.name);
    try testing.expectEqual(@as(u32, 0xf0f8ff), first.rgb);
    try testing.expect(colorNameAt(colorNameCount()) == null);
    const got = parseColor(first.name).?;
    try testing.expectEqual(@as(u8, 0xf0), got.r);
}

test "parseDuration through the ABI" {
    const day: i64 = 24 * 60 * 60 * 1000;
    try testing.expectEqual(day, parseDuration("day").?);
    try testing.expectEqual(@as(i64, 23) * day, parseDuration("days 23").?);
    try testing.expectEqual(@as(i64, 3) * 30 * day, parseDuration("months 3").?);
    try testing.expectEqual(@as(i64, 14) * day, parseDuration("fortnight").?);
    try testing.expectEqual(@as(i64, 0), parseDuration("off").?); // keep forever
    try testing.expect(parseDuration("banana") == null);
    try testing.expect(parseDuration("days 0") == null);
}

test "namedPageSize + blank fill round trips through the ABI" {
    const p = namedPageSize("A4").?;
    try testing.expectApproxEqAbs(@as(f64, 21.0), p.w, 0.01);
    var px = [_]u8{0} ** 8;
    fillRGBA(&px, 2, .{ .r = 10, .g = 20, .b = 30, .a = 40 });
    try testing.expectEqual(@as(u8, 10), px[0]);
    try testing.expectEqual(@as(u8, 40), px[7]);
}

test "pageFormats lists the canonical names; canonicalPageFormat normalizes case" {
    const names = pageFormats();
    try testing.expect(std.mem.startsWith(u8, names, "A0 A1 "));
    try testing.expect(std.mem.indexOf(u8, names, "B5") != null);
    try testing.expect(std.mem.indexOf(u8, names, "C10") != null);
    try testing.expect(std.mem.indexOf(u8, names, "custom") == null);

    try testing.expectEqualStrings("B5", canonicalPageFormat("b5").?);
    try testing.expectEqualStrings("A10", canonicalPageFormat("a10").?);
    try testing.expect(canonicalPageFormat("custom") == null);
    try testing.expect(canonicalPageFormat("nope") == null);
    try testing.expect(canonicalPageFormat("") == null);
    // Every canonical name resolves to a size through the ABI.
    const b5 = namedPageSize(zstr(canonicalPageFormat("B5").?).?).?;
    try testing.expectApproxEqAbs(@as(f64, 17.6), b5.w, 0.01);
    try testing.expectApproxEqAbs(@as(f64, 25.0), b5.h, 0.01);
}

test "invert + contour filters through the ABI" {
    // Invert flips each channel; alpha untouched.
    var px = [_]u8{ 10, 20, 30, 40 };
    applyFilter("invert", &px, 1, .{ .r = 0, .g = 0, .b = 0, .a = 255 });
    try testing.expectEqual(@as(u8, 245), px[0]);
    try testing.expectEqual(@as(u8, 235), px[1]);
    try testing.expectEqual(@as(u8, 225), px[2]);
    try testing.expectEqual(@as(u8, 40), px[3]);
    // Contour on a flat image finds no edges -> uniform white, alpha preserved.
    var flat = [_]u8{ 90, 90, 90, 200 } ** 4; // 2x2, all identical
    applyContour(&flat, 2, 2);
    try testing.expectEqual(@as(u8, 255), flat[0]);
    try testing.expectEqual(@as(u8, 255), flat[1]);
    try testing.expectEqual(@as(u8, 255), flat[2]);
    try testing.expectEqual(@as(u8, 200), flat[3]);
}

//! Typed Zig wrappers over the shared C++ core's extern "C" ABI (../core/cliApi.h).
//! The core owns all geometry / colour / length / raster logic; this file is a thin,
//! allocation-free bridge: every string argument is `[:0]const u8`, so the caller owns
//! the NUL (a literal already has one) instead of the wrapper duping one per call.
//! The row-range slice of the same ABI lives in imageRows.zig, its only consumer.
const std = @import("std");
const formula = @import("core/formula.zig");
const crop = @import("core/crop.zig");
const lines = @import("core/lines.zig");

const c = @cImport({
    @cInclude("core/cliApi.h");
});

pub const Rgba = struct { r: u8, g: u8, b: u8, a: u8 };
pub const Rect = crop.Rect;
pub const Size = crop.Size;
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

/// `mode` as core's case-sensitive filter table keys it ("BW" -> "bw"); null for a tint.
pub fn namedFilter(mode: []const u8) ?[:0]const u8 {
    for ([_][:0]const u8{ "none", "bw", "sepia", "invert", "contour" }) |n| if (std.ascii.eqlIgnoreCase(mode, n)) return n;
    return null;
}

/// Whether `mode` is a filter at all: a named one, or a colour core parses as the tint.
pub fn isFilter(mode: []const u8) bool {
    return namedFilter(mode) != null or parseColor(zstr(mode) orelse return false) != null;
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

pub const resolveCrop = crop.resolveCrop;
pub const cropImageRGBA = crop.cropImageRGBA;
pub const normalizeQuarters = crop.normalizeQuarters;
pub const snapCropRect = crop.snapCropRect;
pub const CropChange = crop.CropChange;
pub const cropChange = crop.cropChange;
pub const EditTurn = crop.EditTurn;
pub const rotateEditQuarter = crop.rotateEditQuarter;
pub const rotatedDims = crop.rotatedDims;
pub const rotateImageRGBA = crop.rotateImageRGBA;
pub const mirrorEdit = crop.mirrorEdit;
pub const mirrorImageRGBA = crop.mirrorImageRGBA;

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

pub const LayoutCaps = lines.LayoutCaps;
pub const layoutCaps = lines.layoutCaps;
pub const mergeKeep = lines.mergeKeep;

pub fn rasterizeLine(buf: []u8, w: i32, h: i32, line: LineDraw) void {
    const n_pts: c_int = @intCast(line.points.len / 2);
    c.stencil_cli_rasterizeLine(buf.ptr, w, h, line.points.ptr, n_pts, line.color.ptr, line.thickness, line.point_size, line.style.ptr, @intFromBool(line.locked), line.fill_color.ptr, line.point_color.ptr);
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

test {
    _ = formula;
    _ = crop;
    _ = lines;
}

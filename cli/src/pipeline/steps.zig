//! The pipeline's steps as standalone building blocks, so the interactive console can
//! drive the same transforms one command at a time: crop → rotate → filter → layout, then
//! encode. The C++ core does every pixel and geometry transform.
const std = @import("std");
const core = @import("../core.zig");
const image = @import("../media/image.zig");
const layout_mod = @import("../media/layout.zig");
const report = @import("../app/report.zig");
const page_mod = @import("../media/page.zig");
const imageRows = @import("../media/imageRows.zig");
const sources = @import("sources.zig");

const loadSource = sources.loadSource;
const loadText = sources.loadText;

/// A decoded source plus the format to fall back to when the output lacks an extension.
pub const Source = struct { img: image.Rgba8, default_fmt: image.Format, bytes: []u8 };

/// Decode an image (or video frame) from a file path or http(s) URL into an owned buffer.
pub fn acquireInput(gpa: std.mem.Allocator, io: std.Io, input: []const u8, frame: u32) !Source {
    const bytes = try loadSource(gpa, io, input, frame);
    errdefer gpa.free(bytes);
    var default_fmt: image.Format = .png;
    const img = image.decode(gpa, bytes) catch |e| {
        report.err("could not decode an image from '{s}' ({s})\n", .{ input, @errorName(e) });
        return e;
    };
    if (image.formatOfPath(input)) |f| default_fmt = f;
    // Keep the raw encoded source bytes (owned by the caller) so a .stencil bundle embeds the
    // untouched original instead of a lossy re-encode.
    return .{ .img = img, .default_fmt = default_fmt, .bytes = bytes };
}

/// Crop in place using a crop spec string; page metrics are derived from the current dims.
pub fn applyCropSpec(gpa: std.mem.Allocator, img: *image.Rgba8, spec: []const u8, album: bool) !void {
    const rect = resolveCropSpec(img.width, img.height, spec, album) orelse return error.BadCrop;
    try cropInPlace(gpa, img, rect);
}

/// Resolve a crop spec to a pixel rect within a `w`×`h` image (page metrics derived from the dims)
/// WITHOUT cropping — for the console's structured model, which records the rect rather than baking it.
pub fn resolveCropSpec(w: usize, h: usize, spec: []const u8, album: bool) ?core.Rect {
    const page = page_mod.pageForImage(w, h);
    const px_per_cm_x = @as(f64, @floatFromInt(w)) / page.w;
    const px_per_cm_y = @as(f64, @floatFromInt(h)) / page.h;
    return core.resolveCrop(core.zstr(spec) orelse "", @floatFromInt(w), @floatFromInt(h), px_per_cm_x, px_per_cm_y, page.w, page.h, album) catch |e| {
        if (e == error.EmptyCrop) {
            report.err("crop spec \"{s}\" leaves nothing of the {d}x{d} image\n", .{ spec, w, h });
        } else report.err("could not parse crop spec \"{s}\" (a bad edge, or one outside the {d}x{d} image)\n", .{ spec, w, h });
        return null;
    };
}

/// Crop in place to an explicit pixel rect, committed inside the image bounds by core's
/// snapCropRect. Used when rebuilding the console's derived view from its recorded crop.
pub fn cropToRect(gpa: std.mem.Allocator, img: *image.Rgba8, rect: core.Rect) !void {
    try cropInPlace(gpa, img, core.snapCropRect(rect, @intCast(img.width), @intCast(img.height)));
}

/// Rotate in place by `rotate` quarter-turns.
pub fn applyRotateBy(gpa: std.mem.Allocator, img: *image.Rgba8, rotate: i32) !void {
    if (@mod(rotate, 4) == 0) return;
    try rotateInPlace(gpa, img, rotate);
}

/// Mirror in place, left-right.
pub fn applyMirror(gpa: std.mem.Allocator, img: *image.Rgba8) !void {
    const dst = try gpa.alloc(u8, img.pixels.len);
    core.mirrorImageRGBA(img.pixels, @intCast(img.width), @intCast(img.height), dst);
    gpa.free(img.pixels);
    img.pixels = dst;
}

/// Load and parse a user-named layout (file path or URL) WITHOUT drawing it, so a caller can read
/// the filter + page pick it carries first; the caller deinits it. The reader reports a file it
/// cannot read; a document that is no layout object, or carries a line it may not, is said here.
pub fn loadLayoutDoc(gpa: std.mem.Allocator, io: std.Io, src: []const u8) !layout_mod.Layout {
    const bytes = try loadText(gpa, io, src);
    defer gpa.free(bytes);
    var doc = layout_mod.parse(gpa, bytes) catch |e| {
        const why = if (e == error.InvalidLayout) "not a JSON object" else @errorName(e);
        report.err("could not read layout '{s}' ({s})\n", .{ src, why });
        return e;
    };
    const bad = doc.bad_line orelse return doc;
    doc.deinit();
    var buf: [96]u8 = undefined;
    report.err("could not read layout '{s}' ({s})\n", .{ src, bad.describe(&buf) });
    return error.InvalidLayoutLine;
}

/// Rasterize a parsed layout's lines onto the image. `source_steps` non-null = the points are in the
/// SOURCE frame (--layout-frame source), re-mapped through the steps and clamped into the bounds.
pub fn drawLayoutDoc(
    gpa: std.mem.Allocator,
    img: *image.Rgba8,
    doc: *const layout_mod.Layout,
    source_steps: ?[]const layout_mod.FrameStep,
) !void {
    for (doc.lines) |line| {
        if (source_steps) |steps| {
            const pts = try gpa.dupe(f64, line.points);
            defer gpa.free(pts);
            layout_mod.remapPoints(pts, steps, img.width, img.height);
            var mapped = line;
            mapped.points = pts;
            core.rasterizeLine(img.pixels, @intCast(img.width), @intCast(img.height), mapped);
            continue;
        }
        core.rasterizeLine(img.pixels, @intCast(img.width), @intCast(img.height), line);
    }
}

/// Apply an image filter in place. "" / "none" is a no-op; the named modes, in any case, are
/// checked before the colour fallback; any other colour name/#hex tints.
pub fn applyFilterMode(gpa: std.mem.Allocator, img: *image.Rgba8, mode: []const u8) void {
    if (mode.len == 0 or std.ascii.eqlIgnoreCase(mode, "none")) return;
    const w: i32 = @intCast(img.width);
    const h: i32 = @intCast(img.height);
    const black = core.Rgba{ .r = 0, .g = 0, .b = 0, .a = 255 };
    // Contour is an edge-detection convolution, not a per-pixel map — it needs the dims.
    if (std.ascii.eqlIgnoreCase(mode, "contour")) return imageRows.contour(gpa, img.pixels, w, h);
    if (core.namedFilter(mode)) |name| return imageRows.filter(name, img.pixels, w, h, black);
    const z = core.zstr(mode) orelse return;
    imageRows.filter(z, img.pixels, w, h, core.parseColor(z) orelse black);
}

// transforms (replace the owned buffer)

pub fn cropInPlace(gpa: std.mem.Allocator, img: *image.Rgba8, rect: core.Rect) !void {
    const uw: usize = @intCast(rect.w);
    const uh: usize = @intCast(rect.h);
    const dst = try gpa.alloc(u8, uw * uh * 4);
    imageRows.crop(img.pixels, @intCast(img.width), @intCast(img.height), rect, dst);
    img.deinit(gpa);
    img.* = .{ .width = uw, .height = uh, .pixels = dst };
}

fn rotateInPlace(gpa: std.mem.Allocator, img: *image.Rgba8, rotate: i32) !void {
    const q = core.normalizeQuarters(rotate);
    const dims = core.rotatedDims(@intCast(img.width), @intCast(img.height), q);
    const uw: usize = @intCast(dims.w);
    const uh: usize = @intCast(dims.h);
    const dst = try gpa.alloc(u8, uw * uh * 4);
    imageRows.rotate(img.pixels, @intCast(img.width), @intCast(img.height), q, dst);
    img.deinit(gpa);
    img.* = .{ .width = uw, .height = uh, .pixels = dst };
}

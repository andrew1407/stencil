//! End-to-end pipeline: acquire a source image (decode a file/URL, grab a video frame,
//! or synthesise a blank), then crop → rotate → draw the layout → filter, and encode the
//! result. The C++ core does every pixel/geometry transform; Zig owns I/O and codecs.
//!
//! The individual steps are exposed as small `pub` building blocks (acquireInput,
//! acquireBlank, applyCropSpec, applyRotateBy, applyLayoutSrc, applyFilterMode,
//! writeOutput) so the interactive console mode (console.zig) can drive the same
//! transforms one command at a time. `run` is just the one-shot composition of them.
const std = @import("std");
const core = @import("core.zig");
const image = @import("image.zig");
const layout_mod = @import("layout.zig");
const video = @import("video.zig");
const net = @import("net.zig");
const args = @import("args.zig");
const logo = @import("logo.zig");

const MAX_FILE = 256 << 20; // 256 MiB read cap for inputs
const BLANK_MIN = 1;
const BLANK_MAX = 8192;

/// A decoded source plus the format to fall back to when the output lacks an extension.
pub const Source = struct { img: image.Rgba8, default_fmt: image.Format };

pub fn run(gpa: std.mem.Allocator, io: std.Io, opts: args.Options) !void {
    // 1) Acquire the source as an owned RGBA8 buffer, and note the format to fall back
    //    to when the output path lacks an extension.
    var img: image.Rgba8 = undefined;
    var default_fmt: image.Format = .png;

    if (opts.blank) |blank| {
        img = try acquireBlank(gpa, blank);
    } else if (opts.input) |input| {
        const src = try acquireInput(gpa, io, input, opts.frame);
        img = src.img;
        default_fmt = src.default_fmt;
    } else {
        logo.print("error: no source — pass --input <path|url> or --blank <w h [color]>\n", .{});
        return error.NoSource;
    }
    defer img.deinit(gpa);

    // 2) Crop, then rotate by N quarter-turns.
    if (opts.crop) |spec| try applyCropSpec(gpa, &img, spec, opts.album);
    try applyRotateBy(gpa, &img, opts.rotate);

    // 3) Layout: draw the lines and capture an optional filter (overridable by --filter).
    var layout_filter_buf: ?[]u8 = null;
    defer if (layout_filter_buf) |b| gpa.free(b);
    if (opts.layout) |src| layout_filter_buf = try applyLayoutSrc(gpa, io, &img, src);

    // 4) Filter — explicit --filter overrides the layout's filter.
    if (opts.filter orelse layout_filter_buf) |f| applyFilterMode(gpa, &img, f);

    // 5) Encode + write.
    const out = opts.output orelse {
        logo.print("error: no output path given\n", .{});
        return error.NoOutput;
    };
    try writeOutput(gpa, io, img, out, default_fmt);
}

// ── steps (each usable standalone by console.zig) ─────────────────────────────

/// Decode an image (or video frame) from a file path or http(s) URL into an owned buffer.
pub fn acquireInput(gpa: std.mem.Allocator, io: std.Io, input: []const u8, frame: u32) !Source {
    const bytes = try loadSource(gpa, io, input, frame);
    defer gpa.free(bytes);
    var default_fmt: image.Format = .png;
    const img = image.decode(gpa, bytes) catch |e| {
        logo.print("error: could not decode an image from '{s}' ({s})\n", .{ input, @errorName(e) });
        return e;
    };
    if (extOf(input)) |e| {
        if (image.formatFromExt(e)) |f| default_fmt = f;
    }
    return .{ .img = img, .default_fmt = default_fmt };
}

/// Crop in place using a crop spec string; page metrics are derived from the current dims.
pub fn applyCropSpec(gpa: std.mem.Allocator, img: *image.Rgba8, spec: []const u8, album: bool) !void {
    const page = pageForImage(gpa, img.width, img.height);
    const px_per_cm_x = @as(f64, @floatFromInt(img.width)) / page.w;
    const px_per_cm_y = @as(f64, @floatFromInt(img.height)) / page.h;
    const rect = core.resolveCrop(gpa, spec, @floatFromInt(img.width), @floatFromInt(img.height), px_per_cm_x, px_per_cm_y, page.w, page.h, album) orelse {
        logo.print("error: could not parse crop spec \"{s}\"\n", .{spec});
        return error.BadCrop;
    };
    try cropInPlace(gpa, img, rect);
}

/// Rotate in place by `rotate` quarter-turns. A multiple of four (incl. 0) is a no-op.
pub fn applyRotateBy(gpa: std.mem.Allocator, img: *image.Rgba8, rotate: i32) !void {
    if (@mod(rotate, 4) == 0) return;
    try rotateInPlace(gpa, img, rotate);
}

/// Draw a layout (file path or URL) onto the image; returns its optional filter name
/// (owned by the caller) so callers can apply or override it.
pub fn applyLayoutSrc(gpa: std.mem.Allocator, io: std.Io, img: *image.Rgba8, src: []const u8) !?[]u8 {
    const bytes = try loadText(gpa, io, src);
    defer gpa.free(bytes);
    var parsed = try layout_mod.parse(gpa, bytes);
    defer parsed.deinit();
    for (parsed.lines) |line| {
        core.rasterizeLine(img.pixels, @intCast(img.width), @intCast(img.height), line);
    }
    if (parsed.filter) |f| return try gpa.dupe(u8, f);
    return null;
}

/// Apply an image filter in place. "" / "none" is a no-op; a colour name/#hex tints.
pub fn applyFilterMode(gpa: std.mem.Allocator, img: *image.Rgba8, mode: []const u8) void {
    if (mode.len == 0 or std.ascii.eqlIgnoreCase(mode, "none")) return;
    const tint = core.parseColor(gpa, mode) orelse core.Rgba{ .r = 0, .g = 0, .b = 0, .a = 255 };
    core.applyFilter(gpa, mode, img.pixels, @intCast(img.width * img.height), tint);
}

/// Encode the image and write it to `out` (extension filled from `default_fmt` if absent),
/// then print the canonical `wrote {path} ({w}x{h})` line.
pub fn writeOutput(gpa: std.mem.Allocator, io: std.Io, img: image.Rgba8, out: []const u8, default_fmt: image.Format) !void {
    const dir = std.Io.Dir.cwd();
    const resolved = try resolveOutput(gpa, out, default_fmt);
    defer gpa.free(resolved.path);

    const encoded = try image.encode(gpa, img, resolved.fmt);
    defer gpa.free(encoded);
    try dir.writeFile(io, .{ .sub_path = resolved.path, .data = encoded });

    logo.print("wrote {s} ({d}x{d})\n", .{ resolved.path, img.width, img.height });
}

// ── source acquisition ───────────────────────────────────────────────────────

fn loadSource(gpa: std.mem.Allocator, io: std.Io, input: []const u8, frame: u32) ![]u8 {
    if (video.looksLikeVideo(input)) {
        return video.extractFrame(gpa, io, input, frame) catch |e| return mapMediaError(e);
    }
    if (net.isUrl(input)) return net.fetch(gpa, io, input) catch |e| return mapMediaError(e);
    return readLocal(gpa, io, input);
}

fn loadText(gpa: std.mem.Allocator, io: std.Io, src: []const u8) ![]u8 {
    if (net.isUrl(src)) return net.fetch(gpa, io, src) catch |e| return mapMediaError(e);
    return readLocal(gpa, io, src);
}

fn readLocal(gpa: std.mem.Allocator, io: std.Io, path: []const u8) ![]u8 {
    const dir = std.Io.Dir.cwd();
    return dir.readFileAlloc(io, path, gpa, .limited(MAX_FILE)) catch |e| {
        logo.print("error: cannot read '{s}': {s}\n", .{ path, @errorName(e) });
        return e;
    };
}

fn mapMediaError(e: anyerror) anyerror {
    switch (e) {
        video.Error.FfmpegMissing => logo.print("error: ffmpeg not found on PATH — needed only for video input\n", .{}),
        else => {},
    }
    return e;
}

// ── blank synthesis ──────────────────────────────────────────────────────────

pub fn acquireBlank(gpa: std.mem.Allocator, blank: args.Blank) !image.Rgba8 {
    const page = core.namedPageSize(gpa, "A4") orelse core.Page{ .w = 21.0, .h = 29.7 };
    var w: i64 = undefined;
    var h: i64 = undefined;
    if (blank.width != null and blank.height != null) {
        w = blank.width.?;
        h = blank.height.?;
    } else {
        const s = core.defaultBlankSizePx(page.w, page.h, 96.0);
        w = s.w;
        h = s.h;
    }
    w = std.math.clamp(w, BLANK_MIN, BLANK_MAX);
    h = std.math.clamp(h, BLANK_MIN, BLANK_MAX);
    const color = core.parseColor(gpa, blank.color) orelse core.Rgba{ .r = 255, .g = 255, .b = 255, .a = 255 };

    const uw: usize = @intCast(w);
    const uh: usize = @intCast(h);
    const pixels = try gpa.alloc(u8, uw * uh * 4);
    core.fillRGBA(pixels, @intCast(uw * uh), color);
    return .{ .width = uw, .height = uh, .pixels = pixels };
}

// ── transforms (replace the owned buffer) ────────────────────────────────────

fn cropInPlace(gpa: std.mem.Allocator, img: *image.Rgba8, rect: core.Rect) !void {
    const uw: usize = @intCast(rect.w);
    const uh: usize = @intCast(rect.h);
    const dst = try gpa.alloc(u8, uw * uh * 4);
    core.cropImageRGBA(img.pixels, @intCast(img.width), @intCast(img.height), rect, dst);
    gpa.free(img.pixels);
    img.* = .{ .width = uw, .height = uh, .pixels = dst };
}

fn rotateInPlace(gpa: std.mem.Allocator, img: *image.Rgba8, rotate: i32) !void {
    const q = core.normalizeQuarters(rotate);
    const dims = core.rotatedDims(@intCast(img.width), @intCast(img.height), q);
    const uw: usize = @intCast(dims.w);
    const uh: usize = @intCast(dims.h);
    const dst = try gpa.alloc(u8, uw * uh * 4);
    core.rotateImageRGBA(img.pixels, @intCast(img.width), @intCast(img.height), q, dst);
    gpa.free(img.pixels);
    img.* = .{ .width = uw, .height = uh, .pixels = dst };
}

// ── page + output helpers ────────────────────────────────────────────────────

fn pageForImage(gpa: std.mem.Allocator, w: usize, h: usize) core.Page {
    const base = core.namedPageSize(gpa, "A4") orelse core.Page{ .w = 21.0, .h = 29.7 };
    // Landscape image -> lay the page on its side, mirroring core pageDimensions.
    if (w > h) return .{ .w = @max(base.w, base.h), .h = @min(base.w, base.h) };
    return .{ .w = @min(base.w, base.h), .h = @max(base.w, base.h) };
}

const Resolved = struct { path: []u8, fmt: image.Format };

// A recognised extension selects the format; otherwise fall back to `fallback` and
// append its extension (so `out` becomes `out.png`, `result` -> `result.jpg`, etc.).
fn resolveOutput(gpa: std.mem.Allocator, out: []const u8, fallback: image.Format) !Resolved {
    if (extOf(out)) |e| {
        if (image.formatFromExt(e)) |f| return .{ .path = try gpa.dupe(u8, out), .fmt = f };
    }
    const path = try std.fmt.allocPrint(gpa, "{s}.{s}", .{ out, fallback.ext() });
    return .{ .path = path, .fmt = fallback };
}

fn extOf(path: []const u8) ?[]const u8 {
    const dot = std.mem.lastIndexOfScalar(u8, path, '.') orelse return null;
    const slash = std.mem.lastIndexOfAny(u8, path, "/\\");
    if (slash) |s| if (dot < s) return null; // the dot is in a directory name
    if (dot + 1 >= path.len) return null;
    return path[dot + 1 ..];
}

//! The pipeline's steps as standalone building blocks, so the interactive console can
//! drive the same transforms one command at a time: crop → rotate → filter → layout, then
//! encode. The C++ core does every pixel and geometry transform.
const std = @import("std");
const core = @import("../core.zig");
const image = @import("../image.zig");
const layout_mod = @import("../layout.zig");
const video = @import("../video.zig");
const net = @import("../net.zig");
const args = @import("../args.zig");
const report = @import("../report.zig");
const confine = @import("../confine.zig");
const page_mod = @import("../page.zig");
const imageRows = @import("../imageRows.zig");
const sources = @import("sources.zig");

const expandHome = sources.expandHome;
const loadSource = sources.loadSource;
const loadText = sources.loadText;
const hasParentTraversal = confine.hasParentTraversal;

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
    return core.resolveCrop(core.zstr(spec) orelse "", @floatFromInt(w), @floatFromInt(h), px_per_cm_x, px_per_cm_y, page.w, page.h, album) orelse {
        report.err("could not parse crop spec \"{s}\"\n", .{spec});
        return null;
    };
}

/// Crop in place to an explicit pixel rect (clamped to the image bounds). Used when rebuilding
/// the console's derived view from its recorded crop.
pub fn cropToRect(gpa: std.mem.Allocator, img: *image.Rgba8, rect: core.Rect) !void {
    const iw: i32 = @intCast(img.width);
    const ih: i32 = @intCast(img.height);
    var r = rect;
    r.w = std.math.clamp(r.w, 1, iw);
    r.h = std.math.clamp(r.h, 1, ih);
    r.x = std.math.clamp(r.x, 0, iw - r.w);
    r.y = std.math.clamp(r.y, 0, ih - r.h);
    try cropInPlace(gpa, img, r);
}

/// Rotate in place by `rotate` quarter-turns.
pub fn applyRotateBy(gpa: std.mem.Allocator, img: *image.Rgba8, rotate: i32) !void {
    if (@mod(rotate, 4) == 0) return;
    try rotateInPlace(gpa, img, rotate);
}

/// Load and parse a layout (file path or URL) WITHOUT drawing it, so a caller can read the optional
/// filter + page pick it carries first. The returned doc owns its arena; the caller deinits it.
pub fn loadLayoutDoc(gpa: std.mem.Allocator, io: std.Io, src: []const u8) !layout_mod.Layout {
    const bytes = try loadText(gpa, io, src);
    defer gpa.free(bytes);
    return layout_mod.parse(gpa, bytes);
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

/// Apply an image filter in place. "" / "none" is a no-op; "invert" and "contour" are named
/// modes (checked before the colour fallback); any other colour name/#hex tints.
pub fn applyFilterMode(gpa: std.mem.Allocator, img: *image.Rgba8, mode: []const u8) void {
    if (mode.len == 0 or std.ascii.eqlIgnoreCase(mode, "none")) return;
    const w: i32 = @intCast(img.width);
    const h: i32 = @intCast(img.height);
    const black = core.Rgba{ .r = 0, .g = 0, .b = 0, .a = 255 };
    // Contour is an edge-detection convolution, not a per-pixel map — it needs the dims.
    if (std.ascii.eqlIgnoreCase(mode, "contour")) return imageRows.contour(gpa, img.pixels, w, h);
    if (std.ascii.eqlIgnoreCase(mode, "invert")) return imageRows.filter("invert", img.pixels, w, h, black);
    const z = core.zstr(mode) orelse return;
    imageRows.filter(z, img.pixels, w, h, core.parseColor(z) orelse black);
}

/// Encode the image, write it to `out` (extension filled from `default_fmt` if absent), and print the
/// canonical `wrote {path} ({w}x{h} px · {page})` line with the given page label.
pub fn writeOutputLabeled(gpa: std.mem.Allocator, io: std.Io, img: image.Rgba8, out: []const u8, default_fmt: image.Format, page_label: []const u8) !void {
    const dir = std.Io.Dir.cwd();
    const resolved = try resolveOutput(gpa, out, default_fmt);
    defer gpa.free(resolved.path);

    const encoded = try image.encode(gpa, img, resolved.fmt);
    defer gpa.free(encoded);
    // The only place a result reaches the disk, so it owns the failure line too: a caller
    // that just propagated would exit 1 with nothing said, and adapters parse `error:`.
    dir.writeFile(io, .{ .sub_path = resolved.path, .data = encoded }) catch |e| {
        report.err("could not write {s} ({s})\n", .{ resolved.path, @errorName(e) });
        return e;
    };

    report.print("wrote {s} ({d}x{d} px · {s})\n", .{ resolved.path, img.width, img.height, page_label });
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

const Resolved = struct { path: []u8, fmt: image.Format };

// A recognised extension selects the format; otherwise fall back to `fallback` and
// append its extension (so `out` becomes `out.png`, `result` -> `result.jpg`, etc.).
fn resolveOutput(gpa: std.mem.Allocator, out_raw: []const u8, fallback: image.Format) !Resolved {
    // A typed "~/Downloads/x.png" means the home directory, not one named "~".
    const out = try expandHome(gpa, out_raw);
    errdefer gpa.free(out);
    // Refuse an output path that climbs above the working directory. Direct users still write anywhere they
    // name; this only blocks the ".." traversal an adapter forwarding an untrusted name should not do.
    if (hasParentTraversal(out)) {
        report.err("refusing to write to a path that escapes the working directory: '{s}'\n", .{out});
        return error.UnsafeOutputPath; // the errdefer above frees `out`
    }
    if (image.extOf(out)) |e| {
        if (image.formatFromExt(e)) |f| return .{ .path = out, .fmt = f };
    }
    defer gpa.free(out);
    const path = try std.fmt.allocPrint(gpa, "{s}.{s}", .{ out, fallback.ext() });
    return .{ .path = path, .fmt = fallback };
}

const testing = std.testing;

test "resolveOutput rejects parent-directory traversal" {
    const gpa = testing.allocator;
    try testing.expectError(error.UnsafeOutputPath, resolveOutput(gpa, "../../etc/evil.png", image.Format.png));
    try testing.expectError(error.UnsafeOutputPath, resolveOutput(gpa, "sub/../../out.png", image.Format.png));
    // A normal relative name is accepted and keeps its extension.
    const ok = try resolveOutput(gpa, "out.png", image.Format.png);
    defer gpa.free(ok.path);
    try testing.expectEqualStrings("out.png", ok.path);
}

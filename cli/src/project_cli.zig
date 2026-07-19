//! One-shot `.stencil` handling — `stencil -i project.stencil out.png` (render) and
//! `stencil -i photo.png … out.stencil` (bundle); reuses the console `Session` so a project's
//! crop/rotation/filter/lines derive exactly as the browser/desktop editors render them.
const std = @import("std");
const args = @import("args.zig");
const pipeline = @import("pipeline.zig");
const net = @import("net.zig");
const project = @import("project.zig");
const logo = @import("logo.zig");
const commands = @import("console/commands.zig");
const Session = @import("console/session.zig").Session;

/// Split a `--filter` value into a Session filter (mode, color): named modes pass through,
/// anything else is a custom tint colour.
fn filterModeColor(f: []const u8) struct { mode: []const u8, color: []const u8 } {
    const named = [_][]const u8{ "none", "bw", "sepia", "invert", "contour" };
    for (named) |n| if (std.ascii.eqlIgnoreCase(f, n)) return .{ .mode = f, .color = "" };
    return .{ .mode = "custom", .color = f };
}

pub fn runOneShot(gpa: std.mem.Allocator, io: std.Io, opts: args.Options) !void {
    var sess = Session{ .gpa = gpa };
    defer sess.deinit();

    // Metadata carried into a re-bundled .stencil output (owned; freed at the end).
    var meta_name: []u8 = &.{};
    var meta_color: []u8 = &.{};
    var meta_source: []u8 = &.{};
    var meta_resource: []u8 = &.{};
    var meta_blank = false;
    var meta_blank_color: []u8 = &.{};
    defer {
        gpa.free(meta_name);
        gpa.free(meta_color);
        gpa.free(meta_source);
        gpa.free(meta_resource);
        gpa.free(meta_blank_color);
    }

    // ── 1) Source ──────────────────────────────────────────────────────────────
    if (opts.input) |input| {
        if (project.isStencilPath(input)) {
            var proj = try project.loadInto(&sess, io, input); // prints its own error
            defer proj.deinit();
            meta_name = try gpa.dupe(u8, proj.name);
            meta_color = try gpa.dupe(u8, proj.color);
            meta_source = try gpa.dupe(u8, proj.source);
            meta_resource = try gpa.dupe(u8, proj.resource);
            meta_blank = proj.blank;
            meta_blank_color = try gpa.dupe(u8, proj.blank_color);
        } else {
            const src = try pipeline.acquireInput(gpa, io, input, opts.frame);
            try sess.loadImage(src.img, input, net.isUrl(input), src.default_fmt, src.bytes);
            meta_name = try gpa.dupe(u8, commands.projectBaseName(input));
            if (net.isUrl(input)) meta_source = try gpa.dupe(u8, input);
        }
    } else if (opts.blank) |blank| {
        const img = try pipeline.acquireBlank(gpa, blank);
        try sess.loadImage(img, "blank", true, .png, null);
        meta_name = try gpa.dupe(u8, "blank");
        meta_blank = true;
        meta_blank_color = try gpa.dupe(u8, blank.color);
    } else {
        logo.print("error: no source — pass --input <path|url> or --blank [format] [w h] [color]\n", .{});
        return error.NoSource;
    }

    // ── 2) Extra flag edits ON TOP (crop → rotate → layout → filter), mirroring pipeline order ──
    if (opts.crop) |spec| {
        const cur = sess.current();
        const rect = pipeline.resolveCropSpec(gpa, cur.width, cur.height, spec, opts.album) orelse return error.BadCrop;
        try sess.applyCrop(rect);
    }
    if (@mod(opts.rotate, 4) != 0) try sess.applyRotate(opts.rotate);
    if (opts.layout) |src| {
        const lb = try pipeline.loadLayoutBytes(gpa, io, src);
        defer gpa.free(lb);
        try sess.addLines(lb);
    }
    if (opts.filter) |f| {
        const mc = filterModeColor(f);
        try sess.setFilter(mc.mode, mc.color);
    }

    // ── 3) Output ──────────────────────────────────────────────────────────────
    const out = opts.output orelse {
        logo.print("error: no output path given\n", .{});
        return error.NoOutput;
    };
    if (project.isStencilPath(out)) {
        try project.saveInto(&sess, io, out, .{
            .name = meta_name,
            .color = meta_color,
            .source = meta_source,
            .resource = meta_resource,
            .blank = meta_blank,
            .blank_color = meta_blank_color,
        });
    } else {
        const page_label = try sess.pageFormatLabel();
        defer gpa.free(page_label);
        try pipeline.writeOutputLabeled(gpa, io, sess.current().*, out, sess.default_fmt, page_label);
    }
}

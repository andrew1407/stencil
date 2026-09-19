//! Contract §9 variants: render each alternative of a plan beside the working image,
//! writing `<stem>.png` per variant without touching the session's own edit state.
const std = @import("std");
const image = @import("../../image.zig");
const pipeline = @import("../../pipeline.zig");
const logo = @import("../../logo.zig");
const core = @import("../../core.zig");
const llm = @import("../../llm.zig");
const layout_mod = @import("../../layout.zig");
const Session = @import("../session.zig").Session;
const planOps = @import("planOps.zig");
const resolvePlanCrop = planOps.resolvePlanCrop;
const rotateQuarters = planOps.rotateQuarters;
const filterArg = planOps.filterArg;
const acquirePlanBlank = planOps.acquirePlanBlank;
const linesDoc = planOps.linesDoc;

pub fn renderVariants(session: *Session, io: std.Io, plan: *const llm.Plan, base_steps: []const layout_mod.FrameStep) void {
    if (plan.variants.len == 0) return;
    const gpa = session.gpa;
    var used: std.ArrayList([]u8) = .empty;
    defer {
        for (used.items) |s| gpa.free(s);
        used.deinit(gpa);
    }
    for (plan.variants, 0..) |v, i| {
        const stem = variantStem(gpa, &used, v.label, i) catch continue;
        renderVariant(session, io, v, stem, base_steps);
    }
}

/// A unique `[a-z0-9-]` file stem for a variant: the sanitized label, falling back to the variant's
/// 1-based position, with a numeric suffix on a collision. Appended to `used`, which owns it.
fn variantStem(gpa: std.mem.Allocator, used: *std.ArrayList([]u8), label: []const u8, i: usize) ![]const u8 {
    var stem = try llm.sanitizeLabel(gpa, label);
    errdefer gpa.free(stem); // frees whatever stem currently holds on any later failure
    if (stem.len == 0) stem = try std.fmt.allocPrint(gpa, "{d}", .{i + 1});
    if (stemTaken(used.items, stem)) {
        var n: usize = 2;
        while (true) : (n += 1) {
            const cand = try std.fmt.allocPrint(gpa, "{s}-{d}", .{ stem, n });
            if (!stemTaken(used.items, cand)) {
                gpa.free(stem);
                stem = cand;
                break;
            }
            gpa.free(cand);
        }
    }
    try used.append(gpa, stem);
    return stem;
}

fn stemTaken(used: []const []u8, stem: []const u8) bool {
    for (used) |s| {
        if (std.mem.eql(u8, s, stem)) return true;
    }
    return false;
}

const PendingLines = struct { json: []const u8, steps_start: usize };

pub fn renderVariant(session: *Session, io: std.Io, v: llm.Variant, stem: []const u8, base_steps: []const layout_mod.FrameStep) void {
    const gpa = session.gpa;
    // Start from the current view WITHOUT its drawn lines: a variant filter recolours the picture,
    // never the annotations, so every line goes on at the end (the session's own layering).
    var img: ?image.Rgba8 = null;
    defer if (img) |*m| m.deinit(gpa);
    var pending: std.ArrayList(PendingLines) = .empty;
    defer pending.deinit(gpa);
    // The variant's coordinates are snapshot-frame too (llm-contract §1): its layout
    // points continue through the top-level steps plus the variant's own crop/rotates.
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(gpa);
    steps.appendSlice(gpa, base_steps) catch return;
    if (session.hasImage()) {
        img = session.viewWithoutLines() catch return;
        // The session's lines are already in the current frame — only this variant's own
        // crop/rotates (the steps appended below) still apply to them.
        const drawn = session.state().lines_json;
        if (drawn.len != 0) pending.append(gpa, .{ .json = drawn, .steps_start = steps.items.len }) catch return;
    }
    // The page the wrote line reports: the session's pick, overridden by a page/blank op.
    var page_size: []const u8 = session.page_size;
    var page_w = session.custom_page_w;
    var page_h = session.custom_page_h;

    for (v.actions) |a| switch (a) {
        .crop => |c| {
            const cur = variantImage(&img, stem) orelse return;
            const rect = resolvePlanCrop(gpa, cur.width, cur.height, c) orelse return; // message printed
            pipeline.cropToRect(gpa, cur, rect) catch return;
            steps.append(gpa, .{ .crop = .{ .x = @floatFromInt(rect.x), .y = @floatFromInt(rect.y) } }) catch return;
        },
        .rotate => |r| {
            const cur = variantImage(&img, stem) orelse return;
            const pre_w = cur.width;
            const pre_h = cur.height;
            pipeline.applyRotateBy(gpa, cur, rotateQuarters(r.dir, r.times)) catch return;
            steps.append(gpa, .{ .rotate = .{
                .quarters = rotateQuarters(r.dir, r.times),
                .w = @floatFromInt(pre_w),
                .h = @floatFromInt(pre_h),
            } }) catch return;
        },
        .filter => |f| {
            const cur = variantImage(&img, stem) orelse return;
            pipeline.applyFilterMode(gpa, cur, filterArg(f));
        },
        .layout => |l| {
            _ = variantImage(&img, stem) orelse return; // message printed
            pending.append(gpa, .{ .json = l.lines_json, .steps_start = 0 }) catch return;
        },
        .formula => logo.note("a formula op has no effect on a rendered variant file — skipped in \"{s}\"\n", .{stem}),
        .page => |pf| {
            if (pf.format.len == 0) {
                page_size = "custom";
                page_w = pf.width;
                page_h = pf.height;
            } else if (core.canonicalPageFormat(pf.format)) |name| {
                page_size = name;
                page_w = 0;
                page_h = 0;
            }
        },
        .blank => |b| {
            const blank = acquirePlanBlank(session, b, stem) orelse return; // message printed
            if (img) |*m| m.deinit(gpa);
            img = blank.img;
            steps.clearRetainingCapacity(); // a fresh picture starts a fresh frame
            pending.clearRetainingCapacity(); // …and drops the lines meant for the old one
            if (blank.custom_w > 0) {
                page_size = "custom";
                page_w = blank.custom_w;
                page_h = blank.custom_h;
            } else if (blank.page) |p| {
                page_size = p;
                page_w = 0;
                page_h = 0;
            }
        },
        .frame => return, // pre-checked by runPlan
        // §2 undo/redo/reset, §2.1 image/save + the console-settings ops are top-level
        // only — a variant carrying one was dropped at parse time, so none reach here.
        .undo, .redo, .reset, .image, .save, .accent, .connect, .disconnect, .reconnect, .delete, .open_url, .open_file, .copy, .clear, .clear_chat => return,
    };

    // Every line last, over the finished picture, each re-mapped through the geometry
    // steps that came after it.
    const rendered = variantImage(&img, stem) orelse return;
    for (pending.items) |p| {
        rasterizeVariantLines(gpa, rendered, p.json, steps.items[@min(p.steps_start, steps.items.len)..]);
    }
    const out = rendered.*;
    // The same page-label derivation the session header / wrote lines share.
    const label = pipeline.pageLabelAlloc(gpa, page_size, page_w, page_h, out.width, out.height) catch return;
    defer gpa.free(label);
    const path = std.fmt.allocPrint(gpa, "variant-{s}.png", .{stem}) catch return;
    defer gpa.free(path);
    pipeline.writeOutputLabeled(gpa, io, out, path, .png, label) catch return;
}

/// The variant's working image, or a printed error when no image exists yet.
fn variantImage(img: *?image.Rgba8, stem: []const u8) ?*image.Rgba8 {
    if (img.*) |*m| return m;
    logo.err("variant \"{s}\" needs a working image — nothing loaded\n", .{stem});
    return null;
}

/// Rasterize a JSON lines ARRAY onto a variant image — the variant-side twin of the session's own
/// line rendering. The snapshot-frame points are re-mapped through `steps` and clamped first (§1).
fn rasterizeVariantLines(gpa: std.mem.Allocator, img: *image.Rgba8, lines_json: []const u8, steps: []const layout_mod.FrameStep) void {
    // No steps to follow ⇒ nothing to re-map: draw the points as given, the way the session's
    // own rebuild does. (Re-mapping through an identity chain would still clamp them inward.)
    const remapped: ?[]u8 = if (steps.len == 0)
        null
    else
        layout_mod.remapLinesArrayAlloc(gpa, lines_json, steps, img.width, img.height) catch return;
    defer if (remapped) |r| gpa.free(r);
    const doc = linesDoc(gpa, remapped orelse lines_json) catch return;
    defer gpa.free(doc);
    var parsed = layout_mod.parse(gpa, doc) catch return;
    defer parsed.deinit();
    for (parsed.lines) |line| core.rasterizeLine(img.pixels, @intCast(img.width), @intCast(img.height), line);
}

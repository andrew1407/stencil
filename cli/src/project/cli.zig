//! One-shot `.stencil` handling — `stencil -i project.stencil out.png` (render) and
//! `stencil -i photo.png … out.stencil` (bundle) — and the one-shot `--prompt` turn; reuses the
//! console `Session` so a project's crop/rotation/filter/lines derive exactly as the
//! browser/desktop editors render them, and a plan runs through the console's own handlers.
const std = @import("std");
const builtin = @import("builtin");
const args = @import("../args.zig");
const pipeline = @import("../pipeline.zig");
const layout_mod = @import("../media/layout.zig");
const net = @import("../net.zig");
const project = @import("../project.zig");
const logo = @import("../app/logo.zig");
const confine = @import("../safety/confine.zig");
const commands = @import("../console/commands.zig");
const llm = @import("../llm.zig");
const llmPrompt = @import("../console/llmPrompt.zig");
const Session = @import("../console/session.zig").Session;

/// Split a `--filter` value into a Session filter (mode, color): named modes pass through,
/// anything else is a custom tint colour.
fn filterModeColor(f: []const u8) struct { mode: []const u8, color: []const u8 } {
    const named = [_][]const u8{ "none", "bw", "sepia", "invert", "contour" };
    for (named) |n| if (std.ascii.eqlIgnoreCase(f, n)) return .{ .mode = f, .color = "" };
    return .{ .mode = "custom", .color = f };
}

pub fn runOneShot(gpa: std.mem.Allocator, io: std.Io, opts: args.Options) !void {
    return runWith(gpa, io, opts, .{});
}

/// `runOneShot` with the `STENCIL_LLM_*` environment a `--prompt` turn resolves its provider from.
pub fn runWith(gpa: std.mem.Allocator, io: std.Io, opts: args.Options, llm_env: llm.Env) !void {
    var sess = Session{ .gpa = gpa, .llm_env = llm_env };
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
        logo.err("no source — pass --input <path|url> or --blank [format] [w h] [color]\n", .{});
        return error.NoSource;
    }

    // Extra flag edits ON TOP of the source, in pipeline order (crop → rotate → layout → filter).
    // --layout-frame source records crop/rotate as frame steps for the re-map (llm-contract.md §1).
    var steps_buf: [2]layout_mod.FrameStep = undefined;
    var n_steps: usize = 0;
    if (opts.crop) |spec| {
        const cur = sess.current();
        const rect = pipeline.resolveCropSpec(cur.width, cur.height, spec, opts.album) orelse return error.BadCrop;
        try sess.applyCrop(rect);
        steps_buf[n_steps] = .{ .crop = .{ .x = @floatFromInt(rect.x), .y = @floatFromInt(rect.y) } };
        n_steps += 1;
    }
    if (@mod(opts.rotate, 4) != 0) {
        const cur = sess.current().*;
        steps_buf[n_steps] = .{ .rotate = .{ .quarters = opts.rotate, .w = @floatFromInt(cur.width), .h = @floatFromInt(cur.height) } };
        n_steps += 1;
        try sess.applyRotate(opts.rotate);
    }
    if (opts.layout) |src| {
        const lb = try pipeline.loadLayoutBytes(gpa, io, src);
        defer gpa.free(lb);
        if (opts.layout_frame == .source) {
            const cur = sess.current().*;
            const remapped = try layout_mod.remapLayoutDocAlloc(gpa, lb, steps_buf[0..n_steps], cur.width, cur.height);
            defer gpa.free(remapped);
            try sess.addLines(remapped);
        } else {
            try sess.addLines(lb);
        }
    }
    if (opts.filter) |f| {
        const mc = filterModeColor(f);
        try sess.setFilter(mc.mode, mc.color);
    }

    const out = opts.output orelse {
        logo.err("no output path given\n", .{});
        return error.NoOutput;
    };
    try checkOutput(gpa, io, opts, &sess, out);
    if (opts.prompt) |text| try runPrompt(gpa, io, opts, &sess, out, text);
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

/// One `--prompt` turn over the loaded input; its key lives in this process only (llm-contract §5).
/// The assistant is the console's, and POSIX-only as the console is.
fn runPrompt(gpa: std.mem.Allocator, io: std.Io, opts: args.Options, sess: *Session, out: []const u8, text: []const u8) !void {
    if (builtin.os.tag == .windows) {
        logo.err("--prompt is not available on Windows — it runs the console's assistant\n", .{});
        return error.PromptFailed;
    }
    if (!try llmPrompt.promptTurn(sess, io, text)) {
        if (sess.llm_cfg != null and sess.llm_cfg.?.provider == .anthropic and sess.llm_cfg.?.api_key.len == 0)
            logo.note("set STENCIL_LLM_API_KEY to your Anthropic API key for this command\n", .{});
        return error.PromptFailed;
    }
    try checkOutput(gpa, io, opts, sess, out); // the plan may have loaded another format
}

/// The output guards, run before anything is sent or written.
fn checkOutput(gpa: std.mem.Allocator, io: std.Io, opts: args.Options, sess: *Session, out: []const u8) !void {
    if (opts.confine_output and confine.escapes(io, std.Io.Dir.cwd(), out)) {
        logo.err("--confine-output: refusing to write outside the working directory: '{s}'\n", .{out});
        return error.UnsafeOutputPath;
    }
    if (opts.no_clobber) try pipeline.refuseClobber(gpa, io, out, sess.default_fmt, project.isStencilPath(out));
}

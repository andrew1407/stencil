//! The one-shot composition `stencil <flags>` runs: acquire the source, transform it,
//! encode it, and — when --remote-update / --remote ask for it — deliver the result to a
//! server project.
const std = @import("std");
const image = @import("../media/image.zig");
const layout_mod = @import("../media/layout.zig");
const confine = @import("../safety/confine.zig");
const net = @import("../net.zig");
const server = @import("../server/client.zig");
const args = @import("../args.zig");
const report = @import("../app/report.zig");
const page_mod = @import("../media/page.zig");
const sources = @import("sources.zig");
const steps_mod = @import("steps.zig");

const acquireInput = steps_mod.acquireInput;
const acquireBlank = sources.acquireBlank;
const applyCropSpec = steps_mod.applyCropSpec;
const resolveCropSpec = steps_mod.resolveCropSpec;
const cropToRect = steps_mod.cropToRect;
const cropInPlace = steps_mod.cropInPlace;
const applyRotateBy = steps_mod.applyRotateBy;
const applyFilterMode = steps_mod.applyFilterMode;
const loadLayoutDoc = steps_mod.loadLayoutDoc;
const drawLayoutDoc = steps_mod.drawLayoutDoc;
const writeOutputLabeled = steps_mod.writeOutputLabeled;

pub fn run(gpa: std.mem.Allocator, io: std.Io, opts: args.Options) !void {
    // 1) Acquire the source as an owned RGBA8 buffer, and note the format to fall back
    //    to when the output path lacks an extension.
    var img: image.Rgba8 = undefined;
    var default_fmt: image.Format = .png;

    // --server makes -i refer to a server PROJECT (fetched as the source) rather than
    // a local path; with --remote-update the result is written back to it.
    var fetch_client: ?server.Client = null;
    defer if (fetch_client) |*c| c.deinit();
    var fetched_id: ?[]u8 = null;
    defer if (fetched_id) |id| gpa.free(id);

    if (opts.server) |url| {
        const name = opts.input orelse {
            report.err("--server needs -i <server project name>\n", .{});
            return error.NoSource;
        };
        fetch_client = server.connect(gpa, io, url, opts.token) catch |e| {
            server.printConnectError(url, e);
            return e;
        };
        const id = (fetch_client.?.findProjectIdByName(name) catch |e| {
            report.err("server lookup failed ({s})\n", .{@errorName(e)});
            return e;
        }) orelse {
            report.err("no server project named \"{s}\"\n", .{name});
            return error.NoSource;
        };
        fetched_id = id;
        const orig = try fetch_client.?.downloadFile(id, "original");
        defer gpa.free(orig);
        img = image.decode(gpa, orig) catch |e| {
            report.err("could not decode server image ({s})\n", .{@errorName(e)});
            return e;
        };
    } else if (opts.blank) |blank| {
        img = try acquireBlank(gpa, blank);
    } else if (opts.input) |input| {
        const src = try acquireInput(gpa, io, input, opts.frame);
        img = src.img;
        default_fmt = src.default_fmt;
        gpa.free(src.bytes); // one-shot raster path doesn't bundle a project — source bytes unneeded
    } else {
        report.err("no source — pass --input <path|url> or --blank [format] [w h] [color]\n", .{});
        return error.NoSource;
    }
    defer img.deinit(gpa);

    // For --remote (push a NEW project), keep the pre-edit original bytes + dims.
    var original_bytes: ?[]u8 = null;
    defer if (original_bytes) |b| gpa.free(b);
    const orig_w: usize = img.width;
    const orig_h: usize = img.height;
    if (opts.remote != null) original_bytes = try image.encode(gpa, img, default_fmt);

    // Crop, then rotate by N quarter-turns. --layout-frame source records both as frame-mapping steps, so
    // step 3 can re-map the layout's SOURCE-frame points (llm-contract.md §1).
    var steps_buf: [2]layout_mod.FrameStep = undefined;
    var n_steps: usize = 0;
    if (opts.crop) |spec| {
        const rect = resolveCropSpec(img.width, img.height, spec, opts.album) orelse return error.BadCrop;
        try cropInPlace(gpa, &img, rect);
        steps_buf[n_steps] = .{ .crop = .{ .x = @floatFromInt(rect.x), .y = @floatFromInt(rect.y) } };
        n_steps += 1;
    }
    if (@mod(opts.rotate, 4) != 0) {
        // Record the PRE-rotate (post-crop) dims the point mapping turns within.
        steps_buf[n_steps] = .{ .rotate = .{ .quarters = opts.rotate, .w = @floatFromInt(img.width), .h = @floatFromInt(img.height) } };
        n_steps += 1;
        try applyRotateBy(gpa, &img, opts.rotate);
    }

    // 3) Layout: load + parse it now, still undrawn, so the optional filter + page pick it
    //    carries are known before step 4 touches a pixel.
    var doc: ?layout_mod.Layout = null;
    defer if (doc) |*d| d.deinit();
    if (opts.layout) |src| doc = try loadLayoutDoc(gpa, io, src);

    // 4) Filter — explicit --filter overrides the layout's filter. It runs on the picture
    //    alone; step 5 then draws the lines over it in their own colours.
    if (opts.filter orelse (if (doc) |*d| d.filter else null)) |f| applyFilterMode(gpa, &img, f);

    // 5) Draw the layout's lines on top of the filtered picture.
    if (doc) |*d| {
        const steps: ?[]const layout_mod.FrameStep = if (opts.layout_frame == .source) steps_buf[0..n_steps] else null;
        try drawLayoutDoc(gpa, &img, d, steps);
    }

    // 6) Encode + write locally.
    const out = opts.output orelse {
        report.err("no output path given\n", .{});
        return error.NoOutput;
    };
    if (opts.confine_output and confine.outsideCwd(out)) {
        report.err("--confine-output: refusing to write outside the working directory: '{s}'\n", .{out});
        return error.UnsafeOutputPath;
    }
    // The page reported in the `wrote` line follows the effective page state: an applied
    // layout's pageSize (custom cm dims included), else a blank's picked format, else A4.
    const page_name = page_mod.effectivePageName(if (doc) |*d| d.page_size else null, if (opts.blank) |b| b.page else null);
    const page_label = try page_mod.pageLabelAlloc(
        gpa,
        page_name,
        if (doc) |*d| d.custom_page_w else 0,
        if (doc) |*d| d.custom_page_h else 0,
        img.width,
        img.height,
    );
    defer gpa.free(page_label);
    try writeOutputLabeled(gpa, io, img, out, default_fmt, page_label);

    // 7) Server result delivery.
    try deliverToServer(gpa, io, opts, img, default_fmt, fetch_client, fetched_id, original_bytes, orig_w, orig_h);
}

/// Push the result (and, for a new project, the original) to a server when the
/// --remote-update / --remote flags ask for it.
fn deliverToServer(
    gpa: std.mem.Allocator,
    io: std.Io,
    opts: args.Options,
    img: image.Rgba8,
    fmt: image.Format,
    fetch_client: ?server.Client,
    fetched_id: ?[]u8,
    original_bytes: ?[]u8,
    orig_w: usize,
    orig_h: usize,
) !void {
    // Mode A: write the result back into the fetched server project.
    if (opts.remote_update) {
        if (fetch_client == null or fetched_id == null) {
            report.err("--remote-update needs --server <url> -i <project>\n", .{});
            return error.NoRemote;
        }
        var c = fetch_client.?;
        const result = try image.encode(gpa, img, fmt);
        defer gpa.free(result);
        try c.uploadFile(fetched_id.?, "result", result, fmt.ext(), img.width, img.height);
        report.print("updated server result for project {s} ({d}x{d})\n", .{ fetched_id.?, img.width, img.height });
    }

    // Mode B: create a NEW project on --remote and upload original + result.
    if (opts.remote) |rurl| {
        var c = server.connect(gpa, io, rurl, opts.token) catch |e| {
            server.printConnectError(rurl, e);
            return e;
        };
        defer c.deinit();
        const name = opts.remote_name orelse baseName(opts.input orelse "image");
        const source = if (opts.input != null and net.isUrl(opts.input.?)) opts.input.? else "";
        const id = try c.createProject(name, source);
        defer gpa.free(id);
        if (original_bytes) |ob| try c.uploadFile(id, "original", ob, fmt.ext(), orig_w, orig_h);
        const result = try image.encode(gpa, img, fmt);
        defer gpa.free(result);
        try c.uploadFile(id, "result", result, fmt.ext(), img.width, img.height);
        report.print("created server project \"{s}\" ({s})\n", .{ name, id });
    }
}

/// Last path component without its extension (for the default --remote project name).
fn baseName(path: []const u8) []const u8 {
    const slash = std.mem.lastIndexOfAny(u8, path, "/\\");
    const base = if (slash) |s| path[s + 1 ..] else path;
    const dot = std.mem.lastIndexOfScalar(u8, base, '.');
    return if (dot) |d| base[0..d] else base;
}

// steps (each usable standalone by console.zig)

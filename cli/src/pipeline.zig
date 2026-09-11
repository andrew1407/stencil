//! End-to-end pipeline: acquire a source image (decode a file/URL, grab a video frame,
//! or synthesise a blank), then crop → rotate → filter → draw the layout, and encode the
//! result. The C++ core does every pixel/geometry transform; Zig owns I/O and codecs.
//!
//! The filter runs BEFORE the layout: it belongs to the picture, while the drawn lines are
//! an overlay that keeps its own colours — the same layering the console session, the
//! `project` mode, pystencil and both GUIs render.
//!
//! The individual steps are exposed as small `pub` building blocks (acquireInput,
//! acquireBlank, applyCropSpec, applyRotateBy, loadLayoutDoc, drawLayoutDoc,
//! applyFilterMode, writeOutputLabeled) so the interactive console mode (console.zig) can
//! drive the same transforms one command at a time. `run` is just the one-shot composition.
const std = @import("std");
const core = @import("core.zig");
const image = @import("image.zig");
const layout_mod = @import("layout.zig");
const video = @import("video.zig");
const net = @import("net.zig");
const server = @import("serverClient.zig");
const args = @import("args.zig");
const report = @import("report.zig");
const confine = @import("confine.zig");
const page_mod = @import("page.zig");
const imageRows = @import("imageRows.zig");

const MAX_FILE = 256 << 20; // 256 MiB read cap for inputs
const BLANK_MIN = 1;
const BLANK_MAX = 8192;

/// A decoded source plus the format to fall back to when the output lacks an extension.
pub const Source = struct { img: image.Rgba8, default_fmt: image.Format, bytes: []u8 };

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
        gpa.free(src.bytes);   // one-shot raster path doesn't bundle a project — source bytes unneeded
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

    // 2) Crop, then rotate by N quarter-turns. --layout-frame source records both as
    //    frame-mapping steps so step 3 can re-map the layout's SOURCE-frame points
    //    (llm-contract.md §1); the default `current` leaves plain CLI behavior unchanged.
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

/// Decode an image (or video frame) from a file path or http(s) URL into an owned buffer.
pub fn acquireInput(gpa: std.mem.Allocator, io: std.Io, input: []const u8, frame: u32) !Source {
    const bytes = try loadSource(gpa, io, input, frame);
    errdefer gpa.free(bytes);
    var default_fmt: image.Format = .png;
    const img = image.decode(gpa, bytes) catch |e| {
        report.err("could not decode an image from '{s}' ({s})\n", .{ input, @errorName(e) });
        return e;
    };
    if (extOf(input)) |e| {
        if (image.formatFromExt(e)) |f| default_fmt = f;
    }
    // Keep the raw encoded source bytes (owned by the caller) so a .stencil bundle embeds the
    // untouched original instead of a lossy re-encode.
    return .{ .img = img, .default_fmt = default_fmt, .bytes = bytes };
}

/// Crop in place using a crop spec string; page metrics are derived from the current dims.
pub fn applyCropSpec(gpa: std.mem.Allocator, img: *image.Rgba8, spec: []const u8, album: bool) !void {
    const rect = resolveCropSpec(img.width, img.height, spec, album) orelse return error.BadCrop;
    try cropInPlace(gpa, img, rect);
}

/// Resolve a crop spec to a pixel rect within a `w`×`h` image (page metrics derived from the
/// dims), without cropping — for the console's structured model, which records the rect rather
/// than baking. Prints + returns null on a bad spec.
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

/// Load and parse a layout (file path or URL) WITHOUT drawing it, so a caller can read the
/// optional filter + page pick it carries — and apply that filter to the bare picture —
/// before the lines go on. The returned doc owns its own arena; the caller deinits it.
pub fn loadLayoutDoc(gpa: std.mem.Allocator, io: std.Io, src: []const u8) !layout_mod.Layout {
    const bytes = try loadText(gpa, io, src);
    defer gpa.free(bytes);
    return layout_mod.parse(gpa, bytes);
}

/// Rasterize a parsed layout's lines onto the image. `source_steps` non-null = the doc's
/// points are in the SOURCE frame (--layout-frame source): each line is re-mapped through
/// the steps and clamped into the image bounds before rasterizing.
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

/// Encode the image, write it to `out` (extension filled from `default_fmt` if absent), and
/// print the canonical `wrote {path} ({w}x{h} px · {page})` line with the given page label
/// (built via pageLabelAlloc, or the console's session label — both share that derivation).
pub fn writeOutputLabeled(gpa: std.mem.Allocator, io: std.Io, img: image.Rgba8, out: []const u8, default_fmt: image.Format, page_label: []const u8) !void {
    const dir = std.Io.Dir.cwd();
    const resolved = try resolveOutput(gpa, out, default_fmt);
    defer gpa.free(resolved.path);

    const encoded = try image.encode(gpa, img, resolved.fmt);
    defer gpa.free(encoded);
    try dir.writeFile(io, .{ .sub_path = resolved.path, .data = encoded });

    report.print("wrote {s} ({d}x{d} px · {s})\n", .{ resolved.path, img.width, img.height, page_label });
}

fn loadSource(gpa: std.mem.Allocator, io: std.Io, input: []const u8, frame: u32) ![]u8 {
    // Only http(s) URLs and local paths are accepted. Reject any other scheme up front so a
    // `.mp4`-looking `ftp://`/`file://`/`rtmp://` string can never be handed to ffmpeg, whose
    // protocol surface is far wider than our in-process fetcher.
    if (net.hasForeignScheme(input)) {
        report.err("unsupported URL scheme in '{s}' — pass an http(s) URL or a local path\n", .{input});
        return error.UnsupportedScheme;
    }
    if (video.looksLikeVideo(input)) {
        return video.extractFrame(gpa, io, input, frame) catch |e| return mapMediaError(e);
    }
    // User-named URL → non-strict (loopback allowed for the user's own dev/fixture server).
    if (net.isUrl(input)) return net.fetch(gpa, io, input, false) catch |e| return mapMediaError(e);
    return readLocal(gpa, io, input);
}

fn loadText(gpa: std.mem.Allocator, io: std.Io, src: []const u8) ![]u8 {
    if (net.isUrl(src)) return net.fetch(gpa, io, src, false) catch |e| return mapMediaError(e);
    return readLocal(gpa, io, src);
}

/// Load a layout JSON document (file path or http(s) URL) as raw bytes — for the console's
/// structured model, which records the lines rather than baking them. Prints on failure.
pub fn loadLayoutBytes(gpa: std.mem.Allocator, io: std.Io, src: []const u8) ![]u8 {
    return loadText(gpa, io, src);
}

fn readLocal(gpa: std.mem.Allocator, io: std.Io, path: []const u8) ![]u8 {
    const home = try expandHome(gpa, path);
    defer gpa.free(home);
    const dir = std.Io.Dir.cwd();
    return dir.readFileAlloc(io, home, gpa, .limited(MAX_FILE)) catch |e| {
        report.err("cannot read '{s}': {s}\n", .{ home, @errorName(e) });
        return e;
    };
}

/// Expand a leading `~` (bare, or `~/…`) to $HOME. The shell does this for a one-shot argv,
/// but a path typed INSIDE the console (or quoted on the command line) reaches us literally,
/// and `~/Downloads/x.png` then means a directory actually named "~". Everything else — and a
/// `~` with no $HOME to expand — is returned unchanged. Owned by the caller either way.
pub fn expandHome(gpa: std.mem.Allocator, path: []const u8) ![]u8 {
    if (!(std.mem.eql(u8, path, "~") or std.mem.startsWith(u8, path, "~/"))) {
        return gpa.dupe(u8, path);
    }
    // libc getenv (like the writes in line_edit): std.posix has none, and the console's
    // environ map does not reach this layer.
    const raw_home = std.c.getenv("HOME") orelse return gpa.dupe(u8, path);
    const home = std.mem.span(raw_home);
    if (home.len == 0) return gpa.dupe(u8, path);
    const rest = path[1..]; // "" for a bare "~", else "/…"
    const base = if (home.len > 1 and home[home.len - 1] == '/') home[0 .. home.len - 1] else home;
    return std.fmt.allocPrint(gpa, "{s}{s}", .{ base, rest });
}

fn mapMediaError(e: anyerror) anyerror {
    switch (e) {
        video.Error.FfmpegMissing => report.err("ffmpeg not found on PATH — needed only for video input\n", .{}),
        else => {},
    }
    return e;
}

pub fn acquireBlank(gpa: std.mem.Allocator, blank: args.Blank) !image.Rgba8 {
    // Explicit dims win; else the picked page format's default size.
    var w: i64 = blank.width orelse 0;
    var h: i64 = blank.height orelse 0;
    if (blank.width == null or blank.height == null) {
        const s = page_mod.blankSizeFor(blank.page, 0, 0);
        w = s.w;
        h = s.h;
    }
    w = std.math.clamp(w, BLANK_MIN, BLANK_MAX);
    h = std.math.clamp(h, BLANK_MIN, BLANK_MAX);
    const color = core.parseColor(core.zstr(blank.color) orelse "") orelse core.Rgba{ .r = 255, .g = 255, .b = 255, .a = 255 };

    const uw: usize = @intCast(w);
    const uh: usize = @intCast(h);
    const pixels = try gpa.alloc(u8, uw * uh * 4);
    core.fillRGBA(pixels, @intCast(uw * uh), color);
    return .{ .width = uw, .height = uh, .pixels = pixels };
}

// transforms (replace the owned buffer)

fn cropInPlace(gpa: std.mem.Allocator, img: *image.Rgba8, rect: core.Rect) !void {
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
    // Refuse an output path that climbs above the working directory. Direct users
    // still write anywhere they name (absolute paths, subdirs); this only blocks the
    // ".." traversal that a caller/adapter forwarding an untrusted name shouldn't do.
    if (hasParentTraversal(out)) {
        report.err("refusing to write to a path that escapes the working directory: '{s}'\n", .{out});
        return error.UnsafeOutputPath; // the errdefer above frees `out`
    }
    if (extOf(out)) |e| {
        if (image.formatFromExt(e)) |f| return .{ .path = out, .fmt = f };
    }
    defer gpa.free(out);
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

/// The `..` guard lives in confine.zig; re-exported for scrape.zig and the console's /save.
pub const hasParentTraversal = confine.hasParentTraversal;

// Page-format policy lives in page.zig; re-exported for the console's call sites.
pub const blankSizeFor = page_mod.blankSizeFor;
pub const effectivePageName = page_mod.effectivePageName;
pub const pageLabelAlloc = page_mod.pageLabelAlloc;

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

test "expandHome: a leading ~ becomes $HOME, everything else is untouched" {
    const a = testing.allocator;
    const home = std.mem.span(std.c.getenv("HOME") orelse return error.SkipZigTest);

    const bare = try expandHome(a, "~");
    defer a.free(bare);
    try testing.expectEqualStrings(home, bare);

    const under = try expandHome(a, "~/Downloads/out.png");
    defer a.free(under);
    const want = try std.fmt.allocPrint(a, "{s}/Downloads/out.png", .{home});
    defer a.free(want);
    try testing.expectEqualStrings(want, under);

    // Not a home reference: a relative path, an absolute one, and a NAME that merely starts
    // with a tilde all pass through as typed.
    for ([_][]const u8{ "out.png", "/tmp/out.png", "~tilde/out.png", "sub/~/out.png" }) |path| {
        const same = try expandHome(a, path);
        defer a.free(same);
        try testing.expectEqualStrings(path, same);
    }
}

test "loadSource rejects foreign URL schemes before any IO" {
    const gpa = testing.allocator;
    // hasForeignScheme rejects these up front, so `io` is never touched.
    try testing.expectError(error.UnsupportedScheme, loadSource(gpa, undefined, "file:///etc/passwd.mp4", 0));
    try testing.expectError(error.UnsupportedScheme, loadSource(gpa, undefined, "ftp://host/clip.png", 0));
}

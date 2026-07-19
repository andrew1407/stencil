//! `.stencil` project files: parse/build the portable single-file JSON format bundling the
//! ORIGINAL image + export layout + metadata (Zig-side codec/JSON, like layout.zig). Also the
//! single home for the session⇄file bridge (loadInto/saveInto) both the console handlers and the
//! one-shot pipeline share, so a project's load/save can't drift between the two entry points.
const std = @import("std");
const image = @import("image.zig");
const net = @import("net.zig");
const pipeline = @import("pipeline.zig");
const logo = @import("logo.zig");
const Session = @import("console/session.zig").Session;

pub const FORMAT = "stencil-project";
pub const VERSION = 1;

pub const Error = error{ NotStencilProject, UnsupportedVersion, NoImage, BadImageData };

/// A parsed `.stencil` document. All slices are owned by `arena`; free with deinit().
pub const Project = struct {
    arena: std.heap.ArenaAllocator,
    name: []const u8 = "Untitled",
    color: []const u8 = "",
    description: []const u8 = "",
    source: []const u8 = "",
    resource: []const u8 = "",
    blank: bool = false,
    blank_color: []const u8 = "",
    image_bytes: []u8 = &.{}, // decoded ENCODED image bytes (still PNG/JPEG/…), ready for image.decode
    image_ext: []const u8 = "png",
    image_w: i64 = 0,
    image_h: i64 = 0,
    layout_json: []const u8 = "{}", // the `layout` sub-object, re-stringified

    pub fn deinit(self: *Project) void {
        self.arena.deinit();
    }
};

/// Fields to bundle a project: `image_bytes` are ENCODED (image.encode'd); empty metadata is omitted.
pub const BuildOpts = struct {
    name: []const u8,
    color: []const u8 = "",
    description: []const u8 = "",
    source: []const u8 = "",
    resource: []const u8 = "",
    blank: bool = false,
    blank_color: []const u8 = "",
    image_bytes: []const u8,
    image_ext: []const u8,
    image_w: usize,
    image_h: usize,
    layout_json: []const u8,
};

/// The MIME type for a `data:` URL, from the CLI's image extension.
fn mimeForExt(ext: []const u8) []const u8 {
    if (std.ascii.eqlIgnoreCase(ext, "png")) return "image/png";
    if (std.ascii.eqlIgnoreCase(ext, "jpg") or std.ascii.eqlIgnoreCase(ext, "jpeg")) return "image/jpeg";
    if (std.ascii.eqlIgnoreCase(ext, "bmp")) return "image/bmp";
    if (std.ascii.eqlIgnoreCase(ext, "webp")) return "image/webp";
    if (std.ascii.eqlIgnoreCase(ext, "gif")) return "image/gif";
    if (std.ascii.eqlIgnoreCase(ext, "tga")) return "image/x-tga";
    return "application/octet-stream";
}

/// True when `path` names a `.stencil` project file (case-insensitive).
pub fn isStencilPath(path: []const u8) bool {
    return std.ascii.endsWithIgnoreCase(path, ".stencil");
}

/// Append `s` to `list` as a JSON string literal (quoted + escaped).
fn appendJsonString(gpa: std.mem.Allocator, list: *std.ArrayList(u8), s: []const u8) !void {
    try list.append(gpa, '"');
    for (s) |ch| {
        switch (ch) {
            '"' => try list.appendSlice(gpa, "\\\""),
            '\\' => try list.appendSlice(gpa, "\\\\"),
            '\n' => try list.appendSlice(gpa, "\\n"),
            '\r' => try list.appendSlice(gpa, "\\r"),
            '\t' => try list.appendSlice(gpa, "\\t"),
            else => {
                if (ch < 0x20) {
                    var buf: [8]u8 = undefined;
                    try list.appendSlice(gpa, std.fmt.bufPrint(&buf, "\\u{x:0>4}", .{ch}) catch unreachable);
                } else try list.append(gpa, ch);
            },
        }
    }
    try list.append(gpa, '"');
}

/// Append `,"key":"val"` to `list` when `val` is non-empty (the optional-metadata-field shape).
fn appendOptStr(gpa: std.mem.Allocator, list: *std.ArrayList(u8), key: []const u8, val: []const u8) !void {
    if (val.len == 0) return;
    try list.append(gpa, ',');
    try appendJsonString(gpa, list, key);
    try list.append(gpa, ':');
    try appendJsonString(gpa, list, val);
}

/// Build a `.stencil` document (owned JSON text) from `opts`. The image rides as a base64
/// `data:` URL; the layout (already valid JSON) is embedded verbatim.
pub fn build(gpa: std.mem.Allocator, opts: BuildOpts) ![]u8 {
    var list: std.ArrayList(u8) = .empty;
    errdefer list.deinit(gpa);

    try list.appendSlice(gpa, "{\"format\":\"" ++ FORMAT ++ "\",\"version\":1,\"name\":");
    try appendJsonString(gpa, &list, if (opts.name.len != 0) opts.name else "Untitled");
    try appendOptStr(gpa, &list, "color", opts.color);
    try appendOptStr(gpa, &list, "description", opts.description);
    try appendOptStr(gpa, &list, "source", opts.source);
    try appendOptStr(gpa, &list, "resource", opts.resource);
    if (opts.blank) {
        try list.appendSlice(gpa, ",\"blank\":true");
        try appendOptStr(gpa, &list, "blankColor", opts.blank_color);
    }
    // image.dataUrl = "data:<mime>;base64,<b64>" — the base64 alphabet + fixed prefix need no
    // JSON escaping, so encode straight into the list's tail (no intermediate b64/data_url copy).
    try list.appendSlice(gpa, ",\"image\":{\"dataUrl\":\"data:");
    try list.appendSlice(gpa, mimeForExt(opts.image_ext));
    try list.appendSlice(gpa, ";base64,");
    const enc = std.base64.standard.Encoder;
    const need = enc.calcSize(opts.image_bytes.len);
    try list.ensureUnusedCapacity(gpa, need);
    _ = enc.encode(list.unusedCapacitySlice()[0..need], opts.image_bytes);
    list.items.len += need;
    try list.append(gpa, '"');
    try list.appendSlice(gpa, ",\"ext\":");
    try appendJsonString(gpa, &list, opts.image_ext);
    const wh = try std.fmt.allocPrint(gpa, ",\"w\":{d},\"h\":{d}}}", .{ opts.image_w, opts.image_h });
    defer gpa.free(wh);
    try list.appendSlice(gpa, wh);
    try list.appendSlice(gpa, ",\"layout\":");
    try list.appendSlice(gpa, if (opts.layout_json.len != 0) opts.layout_json else "{}");
    try list.append(gpa, '}');

    return list.toOwnedSlice(gpa);
}

fn jsonStr(obj: std.json.ObjectMap, key: []const u8) ?[]const u8 {
    if (obj.get(key)) |v| {
        if (v == .string) return v.string;
    }
    return null;
}

fn jsonInt(obj: std.json.ObjectMap, key: []const u8) i64 {
    if (obj.get(key)) |v| {
        return switch (v) {
            .integer => |i| i,
            .float => |f| @intFromFloat(f),
            else => 0,
        };
    }
    return 0;
}

/// Decode the base64 payload of a `data:...;base64,<b64>` URL into owned bytes.
fn decodeDataUrl(a: std.mem.Allocator, url: []const u8) ![]u8 {
    const marker = "base64,";
    const idx = std.mem.indexOf(u8, url, marker) orelse return Error.BadImageData;
    const b64 = std.mem.trim(u8, url[idx + marker.len ..], " \t\r\n");
    const dec = std.base64.standard.Decoder;
    const n = dec.calcSizeForSlice(b64) catch return Error.BadImageData;
    const out = try a.alloc(u8, n);
    dec.decode(out, b64) catch return Error.BadImageData;
    return out;
}

/// Parse + validate a `.stencil` document. `bytes` need not outlive the call (everything
/// used is copied into the returned Project's arena). Caller owns the result (deinit()).
pub fn parse(gpa: std.mem.Allocator, bytes: []const u8) !Project {
    var proj = Project{ .arena = std.heap.ArenaAllocator.init(gpa) };
    errdefer proj.arena.deinit();
    const a = proj.arena.allocator();

    const root = std.json.parseFromSliceLeaky(std.json.Value, a, bytes, .{}) catch return Error.NotStencilProject;
    if (root != .object) return Error.NotStencilProject;
    const obj = root.object;

    const fmt = jsonStr(obj, "format") orelse return Error.NotStencilProject;
    if (!std.mem.eql(u8, fmt, FORMAT)) return Error.NotStencilProject;
    const ver = jsonInt(obj, "version");
    if (ver < 1 or ver > VERSION) return Error.UnsupportedVersion;

    // Image (required): decode its base64 payload now so the caller can image.decode it.
    const img_v = obj.get("image") orelse return Error.NoImage;
    if (img_v != .object) return Error.NoImage;
    const data_url = jsonStr(img_v.object, "dataUrl") orelse return Error.NoImage;
    proj.image_bytes = try decodeDataUrl(a, data_url);
    proj.image_ext = try a.dupe(u8, jsonStr(img_v.object, "ext") orelse "png");
    proj.image_w = jsonInt(img_v.object, "w");
    proj.image_h = jsonInt(img_v.object, "h");

    // Layout (re-stringified into the arena so it survives past `bytes`).
    if (obj.get("layout")) |lv| {
        proj.layout_json = std.json.Stringify.valueAlloc(a, lv, .{}) catch "{}";
    }

    // Metadata (all optional; duped into the arena).
    proj.name = try a.dupe(u8, jsonStr(obj, "name") orelse "Untitled");
    if (jsonStr(obj, "color")) |c| proj.color = try a.dupe(u8, c);
    if (jsonStr(obj, "description")) |d| proj.description = try a.dupe(u8, d);
    if (jsonStr(obj, "source")) |s| proj.source = try a.dupe(u8, s);
    if (jsonStr(obj, "resource")) |r| proj.resource = try a.dupe(u8, r);
    if (obj.get("blank")) |v| {
        if (v == .bool) proj.blank = v.bool;
    }
    if (jsonStr(obj, "blankColor")) |bc| proj.blank_color = try a.dupe(u8, bc);

    return proj;
}

// ── session ⇄ .stencil bridge (shared by console handlers + one-shot pipeline) ────────────

/// Load a `.stencil` at `path` (local file or http(s) URL) into `session`: read its bytes, parse,
/// decode the embedded ORIGINAL image, hand it to the session (retaining the encoded source bytes
/// for lossless re-bundling), then adopt its layout. Returns the parsed Project so callers can
/// read its metadata — the caller owns it and must call deinit(). On any failure a message is
/// printed and the error is returned.
pub fn loadInto(session: *Session, io: std.Io, path: []const u8) !Project {
    const bytes = try pipeline.loadLayoutBytes(session.gpa, io, path); // prints its own error
    defer session.gpa.free(bytes);
    var proj = parse(session.gpa, bytes) catch |e| {
        logo.print("error: '{s}' is not a valid .stencil project ({s})\n", .{ path, @errorName(e) });
        return e;
    };
    errdefer proj.deinit();
    const decoded = image.decode(session.gpa, proj.image_bytes) catch |e| {
        logo.print("error: could not decode the project image in '{s}' ({s})\n", .{ path, @errorName(e) });
        return e;
    };
    const fmt = image.formatFromExt(proj.image_ext) orelse .png;
    const label = if (proj.name.len != 0) proj.name else path;
    // loadImage takes ownership of `decoded` + `sb`; only the pre-handoff dupe needs cleanup here.
    const sb = session.gpa.dupe(u8, proj.image_bytes) catch |e| {
        var d = decoded;
        d.deinit(session.gpa);
        return e;
    };
    try session.loadImage(decoded, label, net.isUrl(path), fmt, sb);
    session.adoptServerLayout(proj.layout_json) catch
        logo.print("warning: ignoring an invalid embedded layout in '{s}'\n", .{path});
    return proj;
}

/// Metadata stamped into a saved `.stencil`; the image + layout always come from the session.
pub const SaveMeta = struct {
    name: []const u8,
    color: []const u8 = "",
    description: []const u8 = "",
    source: []const u8 = "",
    resource: []const u8 = "",
    blank: bool = false,
    blank_color: []const u8 = "",
};

/// Bundle the session's current ORIGINAL image + layout + `meta` into a `.stencil` at `path`.
/// Embeds the untouched source bytes verbatim when present (lossless), else re-encodes from
/// pixels for a synthetic original (blank/clipboard/peer). Prints the `wrote … (project)` line on
/// success (or an error) and returns any error. Assumes `session.original != null` (guard first).
pub fn saveInto(session: *Session, io: std.Io, path: []const u8, meta: SaveMeta) !void {
    if (pipeline.hasParentTraversal(path)) {
        logo.print("error: refusing to write to a path that escapes the working directory: '{s}'\n", .{path});
        return error.UnsafeOutputPath;
    }
    const orig = session.original.?;
    const owned_enc: ?[]u8 = if (session.source_bytes == null)
        image.encode(session.gpa, orig, session.default_fmt) catch |e| {
            logo.print("error: could not encode the project image ({s})\n", .{@errorName(e)});
            return e;
        }
    else
        null;
    defer if (owned_enc) |e| session.gpa.free(e);
    const enc = session.source_bytes orelse owned_enc.?;
    const layout_json = try session.currentLayoutJson();
    defer session.gpa.free(layout_json);
    const bundle = build(session.gpa, .{
        .name = meta.name,
        .color = meta.color,
        .description = meta.description,
        .source = meta.source,
        .resource = meta.resource,
        .blank = meta.blank,
        .blank_color = meta.blank_color,
        .image_bytes = enc,
        .image_ext = session.default_fmt.ext(),
        .image_w = orig.width,
        .image_h = orig.height,
        .layout_json = layout_json,
    }) catch |e| {
        logo.print("error: could not build the project file ({s})\n", .{@errorName(e)});
        return e;
    };
    defer session.gpa.free(bundle);
    std.Io.Dir.cwd().writeFile(io, .{ .sub_path = path, .data = bundle }) catch |e| {
        logo.print("error: could not write project to {s} ({s})\n", .{ path, @errorName(e) });
        return e;
    };
    // No "WxH px" token (like the console's "(layout)") so the mcp/bot `wrote` parsers skip it.
    logo.print("wrote {s} (project)\n", .{path});
}

const testing = std.testing;

// A real 1×1 red PNG data-URL (same fixture the browser test uses).
const RED_1x1 = "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mO4Y2T0HwAFbgJAIh+PxAAAAABJRU5ErkJggg==";

test "parse: accepts a minimal valid project and decodes its image" {
    const a = testing.allocator;
    const doc = "{\"format\":\"stencil-project\",\"version\":1,\"name\":\"red\"," ++
        "\"image\":{\"dataUrl\":\"" ++ RED_1x1 ++ "\",\"ext\":\"png\",\"w\":1,\"h\":1}," ++
        "\"layout\":{\"imageWidth\":1,\"imageHeight\":1,\"lines\":[],\"imageFilter\":\"bw\",\"rotationQuarters\":1}}";
    var p = try parse(a, doc);
    defer p.deinit();
    try testing.expectEqualStrings("red", p.name);
    try testing.expectEqualStrings("png", p.image_ext);
    try testing.expectEqual(@as(i64, 1), p.image_w);
    try testing.expect(p.image_bytes.len > 8); // a real PNG header + data
    try testing.expect(std.mem.indexOf(u8, p.layout_json, "\"imageFilter\":\"bw\"") != null);
}

test "parse: rejects a missing format marker and a too-new version" {
    const a = testing.allocator;
    try testing.expectError(Error.NotStencilProject, parse(a, "{\"version\":1}"));
    const newer = "{\"format\":\"stencil-project\",\"version\":999,\"image\":{\"dataUrl\":\"" ++ RED_1x1 ++ "\"}}";
    try testing.expectError(Error.UnsupportedVersion, parse(a, newer));
    try testing.expectError(Error.NoImage, parse(a, "{\"format\":\"stencil-project\",\"version\":1}"));
}

test "build → parse round-trips image + layout + metadata" {
    const a = testing.allocator;
    // Encode a tiny 2-byte "image" — build only base64s it; parse must return the same bytes.
    const img = [_]u8{ 0xDE, 0xAD, 0xBE, 0xEF };
    const doc = try build(a, .{
        .name = "shot",
        .color = "#7c3aed",
        .description = "a caption",
        .image_bytes = &img,
        .image_ext = "png",
        .image_w = 4,
        .image_h = 2,
        .layout_json = "{\"imageWidth\":4,\"imageHeight\":2,\"lines\":[],\"rotationQuarters\":0}",
    });
    defer a.free(doc);
    var p = try parse(a, doc);
    defer p.deinit();
    try testing.expectEqualStrings("shot", p.name);
    try testing.expectEqualStrings("#7c3aed", p.color);
    try testing.expectEqualStrings("a caption", p.description);
    try testing.expectEqualSlices(u8, &img, p.image_bytes);
    try testing.expectEqual(@as(i64, 4), p.image_w);
    try testing.expect(std.mem.indexOf(u8, p.layout_json, "\"imageWidth\":4") != null);
}

test "isStencilPath matches only .stencil (any case)" {
    try testing.expect(isStencilPath("a/b.stencil"));
    try testing.expect(isStencilPath("X.STENCIL"));
    try testing.expect(!isStencilPath("a.stencil.png"));
    try testing.expect(!isStencilPath("a.json"));
}

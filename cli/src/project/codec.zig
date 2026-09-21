//! The `.stencil` single-file JSON codec: building the document (image as a data URL,
//! layout, metadata) and parsing one back, with the format sentinel and version gate.
const std = @import("std");
const image = @import("../media/image.zig");
const report = @import("../app/report.zig");
const llm = @import("../llm.zig");
const shape = @import("shape.zig");

const Project = shape.Project;
const BuildOpts = shape.BuildOpts;
const Error = shape.Error;
const FORMAT = shape.FORMAT;
const VERSION = shape.VERSION;

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
pub fn appendJsonString(gpa: std.mem.Allocator, list: *std.ArrayList(u8), s: []const u8) !void {
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
pub fn appendOptStr(gpa: std.mem.Allocator, list: *std.ArrayList(u8), key: []const u8, val: []const u8) !void {
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
    // Optional saved chat (llm-contract §12): only written when the /chat opt-in produced one;
    // older readers tolerate the unknown key (the format's rule).
    if (opts.chat_json.len != 0) {
        try list.appendSlice(gpa, ",\"chat\":");
        try list.appendSlice(gpa, opts.chat_json);
    }
    try list.append(gpa, '}');

    return list.toOwnedSlice(gpa);
}

pub fn jsonStr(obj: std.json.ObjectMap, key: []const u8) ?[]const u8 {
    if (obj.get(key)) |v| {
        if (v == .string) return v.string;
    }
    return null;
}

pub fn jsonInt(obj: std.json.ObjectMap, key: []const u8) i64 {
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
pub fn decodeDataUrl(a: std.mem.Allocator, url: []const u8) ![]u8 {
    const prefix = "base64,";
    const idx = std.mem.indexOf(u8, url, prefix) orelse return Error.BadImageData;
    const b64 = std.mem.trim(u8, url[idx + prefix.len ..], " \t\r\n");
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

    // Optional saved chat (llm-contract §12.1), captured the same way; tolerance to a
    // malformed block lives in the §12.1 reader (llm.parseChatDoc), not here.
    if (obj.get("chat")) |cv| {
        if (cv == .object) proj.chat_json = std.json.Stringify.valueAlloc(a, cv, .{}) catch null;
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

// session ⇄ .stencil bridge (shared by console handlers + one-shot pipeline)

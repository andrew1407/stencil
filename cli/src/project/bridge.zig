//! The session⇄file bridge both the console handlers and the one-shot pipeline share, so a
//! project's load and save cannot drift between the two entry points.
const std = @import("std");
const image = @import("../image.zig");
const report = @import("../report.zig");
const llm = @import("../llm.zig");
const net = @import("../net.zig");
const pipeline = @import("../pipeline.zig");
const Session = @import("../console/session.zig").Session;
const shape = @import("shape.zig");
const codec = @import("codec.zig");

const Project = shape.Project;
const Error = shape.Error;
const parse = codec.parse;
const build = codec.build;
const isStencilPath = codec.isStencilPath;

/// Load a `.stencil` at `path` (local file or http(s) URL) into `session`: read its bytes, parse,
/// decode the embedded ORIGINAL image, hand it to the session (retaining the encoded source bytes
/// for lossless re-bundling), then adopt its layout. Returns the parsed Project so callers can
/// read its metadata — the caller owns it and must call deinit(). On any failure a message is
/// printed and the error is returned.
pub fn loadInto(session: *Session, io: std.Io, path: []const u8) !Project {
    const bytes = try pipeline.loadLayoutBytes(session.gpa, io, path); // prints its own error
    defer session.gpa.free(bytes);
    var proj = parse(session.gpa, bytes) catch |e| {
        report.err("'{s}' is not a valid .stencil project ({s})\n", .{ path, @errorName(e) });
        return e;
    };
    errdefer proj.deinit();
    const decoded = image.decode(session.gpa, proj.image_bytes) catch |e| {
        report.err("could not decode the project image in '{s}' ({s})\n", .{ path, @errorName(e) });
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
        report.note("ignoring an invalid embedded layout in '{s}'\n", .{path});
    // §12: a saved chat block restores the conversation (replacing) — only when the /chat
    // opt-in is on; off, the key is ignored. A malformed block silently restores nothing.
    if (session.chat_on) {
        if (proj.chat_json) |cj| {
            if (llm.parseChatDoc(session.gpa, cj)) |turns| {
                if (turns.len != 0) session.adoptChatTurns(turns) else llm.freeTurns(session.gpa, turns);
            } else |_| {}
        }
    }
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
        report.err("refusing to write to a path that escapes the working directory: '{s}'\n", .{path});
        return error.UnsafeOutputPath;
    }
    const orig = session.original.?;
    const owned_enc: ?[]u8 = if (session.source_bytes == null)
        image.encode(session.gpa, orig, session.default_fmt) catch |e| {
            report.err("could not encode the project image ({s})\n", .{@errorName(e)});
            return e;
        }
    else
        null;
    defer if (owned_enc) |e| session.gpa.free(e);
    const enc = session.source_bytes orelse owned_enc.?;
    const layout_json = try session.currentLayoutJson();
    defer session.gpa.free(layout_json);
    // §12: with /chat on and turns saved, the bundle carries the persisted-chat document
    // (text-only, ≤ 32 turns); off — or empty — the key is omitted entirely.
    var chat_doc: []u8 = &.{};
    defer if (chat_doc.len != 0) session.gpa.free(chat_doc);
    if (session.chat_on and session.chat_history.items.len != 0) {
        const saved_at = std.Io.Clock.real.now(io).toMilliseconds();
        chat_doc = llm.chatDocAlloc(session.gpa, session.chat_history.items, saved_at) catch &.{};
    }
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
        .chat_json = chat_doc,
    }) catch |e| {
        report.err("could not build the project file ({s})\n", .{@errorName(e)});
        return e;
    };
    defer session.gpa.free(bundle);
    std.Io.Dir.cwd().writeFile(io, .{ .sub_path = path, .data = bundle }) catch |e| {
        report.err("could not write project to {s} ({s})\n", .{ path, @errorName(e) });
        return e;
    };
    // No "WxH px" token (like the console's "(layout)") so the mcp/bot `wrote` parsers skip it.
    report.print("wrote {s} (project)\n", .{path});
}

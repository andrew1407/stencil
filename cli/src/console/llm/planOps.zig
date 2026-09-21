//! The small operations an op-plan action runs through: the session's own history and
//! server pool (§2/§10), and the pure helpers that turn a plan's fields into the arguments
//! the console's existing operations take.
const std = @import("std");
const image = @import("../../media/image.zig");
const pipeline = @import("../../pipeline.zig");
const server = @import("../../server/client.zig");
const logo = @import("../../app/logo.zig");
const core = @import("../../core.zig");
const commands = @import("../commands.zig");
const llm = @import("../../llm.zig");
const project = @import("../../project.zig");
const Session = @import("../session.zig").Session;
const handlers = @import("../handlers.zig");

/// A plan `connect`: resolve ONLY against servers this session already connected to (§10 — the model
/// can never introduce a host). A known disconnected one reconnects through doConnect.
pub fn planConnect(session: *Session, io: std.Io, want: []const u8) void {
    switch (llm.resolveServer(session.known_servers.items, want)) {
        .index => |i| {
            const url = session.known_servers.items[i];
            if (session.findServer(url) != null) {
                logo.print("already connected to {s}\n", .{url});
                return;
            }
            handlers.doConnect(session, io, url) catch {};
        },
        .ambiguous => logo.note("skipped connect — \"{s}\" matches several of this session's servers; use the full URL\n", .{want}),
        .none => logo.note("skipped connect — \"{s}\" is not a server you connected this session; run '/connect <url>' yourself\n", .{want}),
    }
}

/// A plan `disconnect`: resolved against the LIVE connections (like §10 — exact URL,
/// else unique host), then through the same path the /disconnect command takes.
pub fn planDisconnect(session: *Session, want: []const u8) void {
    const gpa = session.gpa;
    var urls: std.ArrayList([]const u8) = .empty;
    defer urls.deinit(gpa);
    for (session.servers.items) |*c| urls.append(gpa, c.base) catch return;
    switch (llm.resolveServer(urls.items, want)) {
        .index => |i| {
            // Resolve to the base COPY first — doDisconnect frees the client it drops.
            const base = gpa.dupe(u8, urls.items[i]) catch return;
            defer gpa.free(base);
            handlers.doDisconnect(session, base) catch {};
        },
        .ambiguous => logo.note("skipped disconnect — \"{s}\" matches several connected servers; use the full URL\n", .{want}),
        .none => logo.note("skipped disconnect — not connected to \"{s}\" ('/connections' lists them)\n", .{want}),
    }
}

/// A plan `reconnect`: connect's §10 resolution (exact URL, else unique host, over the servers the
/// user /connect-ed THIS session), then the path the /reconnect command takes. Misses are notes.
pub fn planReconnect(session: *Session, io: std.Io, want: []const u8) void {
    switch (llm.resolveServer(session.known_servers.items, want)) {
        .index => |i| handlers.doReconnect(session, io, session.known_servers.items[i]) catch {},
        .ambiguous => logo.note("skipped reconnect — \"{s}\" matches several of this session's servers; use the full URL\n", .{want}),
        .none => logo.note("skipped reconnect — \"{s}\" is not a server you connected this session; run '/connect <url>' yourself\n", .{want}),
    }
}

/// A plan §2 undo/redo: step the edit history up to `steps` HISTORY entries through the /undo and
/// /redo acknowledgement path. Steps running out mid-way is a note (§2), never a failed plan.
pub fn planStep(session: *Session, comptime redo: bool, steps: u8) void {
    var moved: u8 = 0;
    if (session.hasImage()) {
        while (moved < steps) : (moved += 1) {
            const ok = if (redo) session.redo() else session.undo();
            if (!ok) break;
        }
    }
    handlers.doStep(
        session,
        moved != 0,
        if (redo) "redone" else "undone",
        if (redo) "nothing to redo (at the latest edit)" else "nothing to undo (at the original)",
    );
    if (moved != 0 and moved < steps)
        logo.note("only {d} of {d} {s} step(s) were available\n", .{ moved, steps, if (redo) "redo" else "undo" });
}

/// Where a §2.1 `save` writes: `<name>.stencil` in the cwd — name from the action, else the ACTIVE
/// attachment, else the working image's label — suffixed " 2"/" 3"… on collision. Caller owns it.
pub fn planSavePath(session: *Session, io: std.Io, name: []const u8, active: ?usize, dest: []const u8) ?[]u8 {
    const gpa = session.gpa;
    // A destination the user named (runPlan already checked they wrote it): a file path is
    // taken as given, a folder gets "<name>.png" — the format /save writes by default.
    if (dest.len != 0) {
        const home = pipeline.expandHome(gpa, dest) catch return null;
        if (looksLikeFilePath(home)) return home;
        defer gpa.free(home);
        const stem = saveStem(session, name, active) orelse return null;
        const sep: []const u8 = if (home.len != 0 and home[home.len - 1] == '/') "" else "/";
        return std.fmt.allocPrint(gpa, "{s}{s}{s}.png", .{ home, sep, stem }) catch null;
    }
    const stem = saveStem(session, name, active) orelse return null;
    const dir = std.Io.Dir.cwd();
    var path = std.fmt.allocPrint(gpa, "{s}.stencil", .{stem}) catch return null;
    var n: usize = 2;
    while (n < 100) : (n += 1) {
        dir.access(io, path, .{}) catch break; // free name (or unreadable) → take it
        gpa.free(path);
        path = std.fmt.allocPrint(gpa, "{s} {d}.stencil", .{ stem, n }) catch return null;
    }
    return path;
}

/// The project stem an unnamed plan save falls back to: the op's name, else the adopted attachment's
/// label, else the session label. Directory and extension drop, so "../x.png" cannot steer the write.
fn saveStem(session: *Session, name: []const u8, active: ?usize) ?[]const u8 {
    const from_attachment = if (active) |i| session.attachments.items[i - 1].label else "";
    const raw = if (name.len != 0) name else if (from_attachment.len != 0) from_attachment else (session.label orelse "project");
    const stem = commands.projectBaseName(std.mem.trim(u8, raw, " \t"));
    if (stem.len == 0 or std.mem.trim(u8, stem, ".").len == 0) {
        logo.note("skipped save — \"{s}\" is not a usable project name\n", .{raw});
        return null;
    }
    return stem;
}

/// Whether a save destination names a FILE (an extension this app writes) rather than a folder.
fn looksLikeFilePath(path: []const u8) bool {
    if (path.len == 0 or path[path.len - 1] == '/') return false;
    return llm.understoodPath(path);
}

/// Map the contract's rotate op onto the session's clockwise quarter-turn count
/// (`right` = clockwise, matching `/rotate n`).
pub fn rotateQuarters(dir: llm.Dir, times: u8) i32 {
    const n: i32 = times;
    return if (dir == .right) n else -n;
}

/// The `/filter`-style argument for a plan filter op (`custom` passes the tint colour as
/// the filter value, per the contract's CLI mapping).
pub fn filterArg(f: anytype) []const u8 {
    return if (f.mode == .custom) f.tint else @tagName(f.mode);
}

/// The page a plan blank op lands on: its explicit format, else the session's picked page — the
/// fallback a bare '/blank <color>' uses. The canonical name is core-owned, so it survives a load.
fn blankPage(session: *const Session, format: ?[]const u8) ?[]const u8 {
    return core.canonicalPageFormat(format orelse session.page_size);
}

/// Resolve a plan crop op against a view size: edges serialized to the `/crop` spec grammar and
/// routed through the same pipeline resolver ("album" rides as the same modifier flag, §10).
pub fn resolvePlanCrop(gpa: std.mem.Allocator, w: usize, h: usize, c: llm.CropEdges) ?core.Rect {
    const spec = llm.cropSpecString(gpa, c) catch return null;
    defer gpa.free(spec);
    return pipeline.resolveCropSpec(w, h, spec, c.album);
}

pub const PlanBlank = struct { img: image.Rgba8, page: ?[]const u8, custom_w: f64 = 0, custom_h: f64 = 0 };

/// Validate a plan blank op's colour and acquire its page image + canonical page name; §2 cm dims
/// override the format and come back as custom_w/h. Null on failure; caller owns `img`.
pub fn acquirePlanBlank(session: *const Session, b: anytype, variant: ?[]const u8) ?PlanBlank {
    const gpa = session.gpa;
    if (core.parseColor(core.zstr(b.color) orelse "") == null) {
        if (variant) |stem|
            logo.err("unknown blank colour '{s}' in variant \"{s}\"\n", .{ b.color, stem })
        else
            logo.err("unknown blank colour '{s}'\n", .{b.color});
        return null;
    }
    if (b.width > 0 and b.height > 0) {
        const s = pipeline.blankSizeFor(null, b.width, b.height);
        const img = pipeline.acquireBlank(gpa, .{ .width = @intCast(s.w), .height = @intCast(s.h), .color = b.color }) catch return null;
        return .{ .img = img, .page = null, .custom_w = b.width, .custom_h = b.height };
    }
    const page = blankPage(session, b.format);
    const img = pipeline.acquireBlank(gpa, .{ .page = page, .color = b.color }) catch return null;
    return .{ .img = img, .page = page };
}

/// Wrap a validated plan lines ARRAY into the `{"lines":[…]}` document shape the session's
/// /apply path (layout.zig) consumes. Caller owns the result.
pub fn linesDoc(gpa: std.mem.Allocator, lines_json: []const u8) error{OutOfMemory}![]u8 {
    return std.fmt.allocPrint(gpa, "{{\"lines\":{s}}}", .{lines_json});
}

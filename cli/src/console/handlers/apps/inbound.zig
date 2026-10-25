//! A launch link handed to `/open` (contracts/launch/launch-uri.md): the browser app's URL or a
//! stencil:// link. Connections first, then the picture (a server project by id or name, a
//! picture URL, a blank), then the layout, the name, the assistant settings and the project
//! fields, each through the verb a typed line would reach; what has no verb here is said.
const std = @import("std");
const uri = @import("../../../params/uri.zig");
const uriExpand = @import("../../../params/uriExpand.zig");
const pipeline = @import("../../../pipeline.zig");
const server_urls = @import("../../../server/urls.zig");
const logo = @import("../../../app/logo.zig");
const msg = @import("../../../app/messages.zig");
const handlers = @import("../../handlers.zig");
const media = @import("../media.zig");
const llmPrompt = @import("../../llmPrompt.zig");
const llm_cfg = @import("../../../llm.zig");
const remoteEvents = @import("../../remoteEvents.zig");
const ui = @import("../../ui.zig");
const Session = @import("../../session.zig").Session;

pub const isLaunchUri = uriExpand.looksLikeLaunchUri;

/// The paired token of the connect entry naming `origin`, else none.
fn tokenFor(q: uri.LaunchUri, origin: []const u8) []const u8 {
    for (q.connect) |c| if (std.ascii.eqlIgnoreCase(c.url, origin)) return c.token;
    return "";
}

fn connectArg(a: std.mem.Allocator, url: []const u8, token: []const u8) ![]const u8 {
    return if (token.len > 0) std.fmt.allocPrint(a, "{s} {s}", .{ url, token }) else url;
}

/// Connected to `origin` (dialled with the link's token when not yet): the client's base, or null once told.
fn ensureConnected(session: *Session, io: std.Io, a: std.mem.Allocator, origin: []const u8, token: []const u8) !?[]const u8 {
    const base = try server_urls.normalizeBase(a, origin);
    if (session.findServer(base) == null) try handlers.doConnectFromLink(session, io, try connectArg(a, origin, token));
    if (session.findServer(base) == null) {
        logo.err(msg.launch_server_needs_connection, .{base});
        return null;
    }
    return base;
}

/// `/fetch <ref> <base>`: the project by id or name, with the layout, colour, chat and feed it adopts.
fn fetch(session: *Session, io: std.Io, a: std.mem.Allocator, origin: []const u8, ref: []const u8, token: []const u8) !void {
    const base = (try ensureConnected(session, io, a, origin, token)) orelse return;
    try handlers.doFetch(session, io, try std.fmt.allocPrint(a, "{s} {s}", .{ ref, base }));
}

fn openPicture(session: *Session, io: std.Io, a: std.mem.Allocator, q: uri.LaunchUri) !void {
    const picture = q.picture orelse return;
    switch (picture) {
        .source => |s| {
            const src = pipeline.acquireInput(session.gpa, io, s.url, s.frameIndex() orelse 0) catch return; // message already printed
            try session.loadImage(src.img, s.url, true, src.default_fmt, src.bytes);
            ui.redraw(session);
        },
        .blank => |b| {
            const spec = if (b.sizePx()) |size|
                try std.fmt.allocPrint(a, "{d} {d} {s}", .{ size[0], size[1], b.color })
            else
                b.color;
            try media.doBlank(session, spec);
        },
        .server => |s| try fetch(session, io, a, s.url, s.id, tokenFor(q, s.url)),
        .project => |p| {
            const origin = q.projectOrigin(p.server) orelse return logo.err(msg.launch_project_needs_server, .{p.ref});
            try fetch(session, io, a, origin, p.ref, tokenFor(q, origin));
        },
    }
}

/// `scheme://host[:port]` of an http(s) URL, the whole string when it has no "://".
fn originOf(url: []const u8) []const u8 {
    const t = llm_cfg.trimUrl(url);
    const after = (std.mem.indexOf(u8, t, "://") orelse return t) + 3;
    const end = std.mem.indexOfAnyPos(u8, t, after, "/?#") orelse t.len;
    return t[0..end];
}

fn sameOrigin(a: []const u8, b: []const u8) bool {
    return std.ascii.eqlIgnoreCase(originOf(a), originOf(b));
}

/// A secret belongs to the endpoint it was given for (launch-uri.md §2): a link that moves the
/// base URL drops the API key unless it brings its own, and one that moves the server drops its token.
fn applyLlm(session: *Session, io: std.Io, a: std.mem.Allocator, llm: uri.Llm) !void {
    const cfg = try session.llmConfig(io);
    const was_base = try a.dupe(u8, cfg.base_url);
    const was_server = try a.dupe(u8, cfg.server_url);
    const words = [_]struct { word: []const u8, value: ?[]const u8 }{
        .{ .word = "provider", .value = llm.provider }, .{ .word = "url", .value = llm.base_url },
        .{ .word = "model", .value = llm.model },       .{ .word = "server", .value = llm.server_url },
        .{ .word = "key", .value = llm.api_key },
    };
    for (words) |w| if (w.value) |v| try llmPrompt.doLlm(session, io, try std.fmt.allocPrint(a, "{s} {s}", .{ w.word, v }));
    if (llm.api_key == null and cfg.api_key.len != 0 and !sameOrigin(was_base, cfg.base_url)) {
        cfg.forgetKey(session.gpa);
        logo.note(msg.launch_llm_secret_dropped, .{ cfg.base_url, "API key" });
    }
    if (cfg.server_token.len != 0 and !sameOrigin(was_server, cfg.server_url)) {
        cfg.forgetServerToken(session.gpa);
        logo.note(msg.launch_llm_secret_dropped, .{ cfg.server_url, "server token" });
    }
}

fn applyProjectFields(session: *Session, q: uri.LaunchUri) !void {
    if (q.description == null and q.keywords == null and q.color == null and q.expire == null) return;
    if (session.remote_id == null) return logo.note(msg.launch_needs_server_project, .{"description, keywords, colour and expiry"});
    if (q.description) |d| try handlers.doProjectDescription(session, d);
    if (q.keywords) |k| try handlers.doKeywordsAdd(session, k);
    if (q.color) |c| try handlers.doProjectColor(session, c);
    // One pasted link must not expire a project its collaborators share (launch-uri.md §2).
    if (q.expire != null) logo.note(msg.launch_left_out, .{"the expiry (a link never expires a server project)"});
}

/// False when the URL is a plain picture after all, so `/open` fetches it as one.
pub fn open(session: *Session, io: std.Io, arg: []const u8) !bool {
    var arena = std.heap.ArenaAllocator.init(session.gpa);
    defer arena.deinit();
    const a = arena.allocator();
    const q = (try uri.parse(a, arg)) orelse return false;
    if (!q.actionable()) return false;
    for (q.warnings) |w| switch (w.code) {
        .secret_in_query => logo.note(msg.launch_secret_in_query, .{w.key}),
        .bad_json => logo.note(msg.launch_bad_json, .{w.key}),
        .bad_value => logo.note(msg.launch_bad_value, .{w.key}),
    };
    if (q.theme != null or q.settings.len > 0) logo.note(msg.launch_left_out, .{"the look and editor settings"});
    if (q.chat_open or q.chat_dock != null) logo.note(msg.launch_left_out, .{"the chat panel"});
    if (q.script.len > 0) logo.note(msg.launch_left_out, .{"the script"});
    for (q.connect) |c| try handlers.doConnectFromLink(session, io, try connectArg(a, c.url, c.token));
    try applyLlm(session, io, a, q.llm);
    try openPicture(session, io, a, q);
    if (q.layout) |l| {
        if (!session.hasImage()) {
            logo.note(msg.launch_left_out, .{"the layout (no picture to put it on)"});
        } else if (handlers.runAction(session, io, .{ .kind = .layout, .arg = l.text() })) remoteEvents.markDirty(session);
    }
    if (q.name) |n| if (session.hasImage()) session.setLabel(n) catch {};
    try applyProjectFields(session, q);
    return true;
}

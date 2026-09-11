//! `/connect`, `/disconnect`, `/reconnect` and `/connections`: the session's pool of
//! collaboration-server clients. Only URLs the user typed are ever dialled.
const std = @import("std");
const server = @import("../../serverClient.zig");
const logo = @import("../../logo.zig");
const commands = @import("../commands.zig");
const project = @import("../../project.zig");
const msg = @import("../../messages.zig");
const Session = @import("../session.zig").Session;

/// `/connect <url [token][ url2 ...]>` — open one or more server connections for the
/// session; a token word after a URL authenticates against a gated server (session or
/// admin token — an admin one mints a session).
pub fn doConnect(session: *Session, io: std.Io, arg: []const u8) !void {
    if (arg.len == 0) {
        logo.err(msg.connect_needs_url, .{});
        return;
    }
    const pairs = try commands.parseConnectArgs(session.gpa, arg);
    defer session.gpa.free(pairs);
    for (pairs) |p| {
        var client = server.connect(session.gpa, io, p.url, p.token) catch |e| {
            server.printConnectError(p.url, e);
            continue;
        };
        if (session.findServer(client.base) != null) {
            logo.print(msg.already_connected, .{client.base});
            client.deinit();
            continue;
        }
        try session.servers.append(session.gpa, client);
        session.rememberServer(client.base) catch {}; // the pool a plan `connect` resolves against
        logo.print(msg.connected, .{client.base});
    }
}

/// `/disconnect [url]` — close one connection (or the most recent when omitted).
pub fn doDisconnect(session: *Session, arg: []const u8) !void {
    if (session.servers.items.len == 0) {
        logo.err(msg.no_server_connections, .{});
        return;
    }
    if (arg.len == 0) {
        const last = &session.servers.items[session.servers.items.len - 1];
        if (session.events_url != null and std.mem.eql(u8, session.events_url.?, last.base)) session.closeEvents();
        logo.print(msg.disconnected, .{last.base});
        last.deinit();
        _ = session.servers.pop();
        return;
    }
    const base = try server.normalizeBase(session.gpa, arg);
    defer session.gpa.free(base);
    if (session.dropServer(base)) {
        logo.print(msg.disconnected, .{base});
    } else {
        logo.print(msg.not_connected_to, .{base});
    }
}

/// `/reconnect [url]` — re-establish one connection (or all): re-issue the auth token and,
/// for the active project's server while syncing, revive the live edit-events feed.
pub fn doReconnect(session: *Session, io: std.Io, arg: []const u8) !void {
    if (session.servers.items.len == 0) {
        logo.err(msg.no_server_connections, .{});
        return;
    }
    if (arg.len == 0) {
        var ok: usize = 0;
        for (0..session.servers.items.len) |i| {
            if (reconnectAt(session, io, i)) ok += 1;
        }
        logo.print(msg.reconnected_count, .{ ok, session.servers.items.len });
        return;
    }
    const base = try server.normalizeBase(session.gpa, arg);
    defer session.gpa.free(base);
    const idx = session.indexOfServer(base) orelse {
        logo.print(msg.not_connected_connect_first, .{base});
        return;
    };
    _ = reconnectAt(session, io, idx);
}

/// Reconnect the server at `i` in place: a fresh client (new token, reusing any user-supplied
/// credential) swapped for the old, reviving the events feed if it hosts the active project.
fn reconnectAt(session: *Session, io: std.Io, i: usize) bool {
    // Copy base + credential first — the reconnect frees the old client (and its slices).
    const base = session.gpa.dupe(u8, session.servers.items[i].base) catch return false;
    defer session.gpa.free(base);
    const cred = session.gpa.dupe(u8, session.servers.items[i].credential) catch return false;
    defer session.gpa.free(cred);
    const fresh = server.connect(session.gpa, io, base, if (cred.len != 0) cred else null) catch |e| {
        logo.err(msg.reconnect_failed, .{ base, @errorName(e) });
        return false;
    };
    const was_events = session.events_url != null and std.mem.eql(u8, session.events_url.?, base);
    const is_active = session.remote_url != null and std.mem.eql(u8, session.remote_url.?, base);
    session.servers.items[i].deinit();
    session.servers.items[i] = fresh;
    if (was_events or is_active) session.openEvents(&session.servers.items[i]); // feed stays open even with sync off
    logo.print(msg.reconnected, .{base});
    return true;
}

/// Which credential kinds `/connections` lists.
pub const ConnFilter = enum { all, admin, session };

/// Parse the optional `/connections` argument: "" (or "all") lists everything, "admin" only
/// admin-credential connections, "session" the rest. Null = unrecognised (caller prints usage).
pub fn parseConnFilter(arg: []const u8) ?ConnFilter {
    const w = std.mem.trim(u8, arg, " \t\r\n");
    if (w.len == 0 or std.ascii.eqlIgnoreCase(w, "all")) return .all;
    if (std.ascii.eqlIgnoreCase(w, "admin")) return .admin;
    if (std.ascii.eqlIgnoreCase(w, "session")) return .session;
    return null;
}

/// True when a connection of `kind` belongs in a listing filtered by `f`.
pub fn connFilterMatches(f: ConnFilter, kind: server.CredentialKind) bool {
    return switch (f) {
        .all => true,
        .admin => kind == .admin,
        .session => kind != .admin, // non-admin: a plain session token, or none at all
    };
}

/// `/connections [admin|session]` — list the connected servers, each with a live
/// reachability status (a quick GET probe per server), an `[admin]` tag when the
/// credential is an admin token, and a badge for the active project's server.
pub fn doConnections(session: *Session, arg: []const u8) void {
    const filter = parseConnFilter(arg) orelse {
        logo.err(msg.connections_usage, .{});
        return;
    };
    if (session.servers.items.len == 0) {
        logo.print(msg.no_server_connections, .{});
        return;
    }
    var shown: usize = 0;
    for (session.servers.items) |*c| {
        if (connFilterMatches(filter, c.credential_kind)) shown += 1;
    }
    if (shown == 0) {
        logo.print(msg.no_filtered_connections, .{ @tagName(filter), session.servers.items.len });
        return;
    }
    logo.print(msg.connections_count, .{shown});
    for (session.servers.items) |*c| {
        if (!connFilterMatches(filter, c.credential_kind)) continue;
        const active = session.remote_url != null and std.mem.eql(u8, session.remote_url.?, c.base);
        logo.print("  {s}{s}  [{s}]{s}\n", .{
            c.base,
            if (c.credential_kind == .admin) "  [admin]" else "",
            probeStatus(c),
            if (active) "  (active project)" else "",
        });
    }
}

/// Probe one server's reachability for the `/connections` status column: a cheap GET that
/// distinguishes a live server from an expired token or an unreachable host.
fn probeStatus(c: *server.Client) []const u8 {
    const body = c.listProjects() catch |e| return switch (e) {
        server.Error.Unauthorized => "auth expired — /reconnect",
        else => "unreachable",
    };
    c.gpa.free(body);
    return "connected";
}

const testing = std.testing;

test "parseConnFilter: blank/all lists everything, admin+session filter, junk = usage" {
    try testing.expectEqual(ConnFilter.all, parseConnFilter("").?);
    try testing.expectEqual(ConnFilter.all, parseConnFilter("  ").?);
    try testing.expectEqual(ConnFilter.all, parseConnFilter("ALL").?);
    try testing.expectEqual(ConnFilter.admin, parseConnFilter("admin").?);
    try testing.expectEqual(ConnFilter.session, parseConnFilter(" Session ").?);
    try testing.expect(parseConnFilter("bogus") == null);
    try testing.expect(parseConnFilter("adminx") == null);

    // "session" means non-admin, which includes an anonymously-minted connection.
    try testing.expect(connFilterMatches(.all, .none) and connFilterMatches(.all, .admin));
    try testing.expect(connFilterMatches(.admin, .admin));
    try testing.expect(!connFilterMatches(.admin, .session) and !connFilterMatches(.admin, .none));
    try testing.expect(connFilterMatches(.session, .session) and connFilterMatches(.session, .none));
    try testing.expect(!connFilterMatches(.session, .admin));
}

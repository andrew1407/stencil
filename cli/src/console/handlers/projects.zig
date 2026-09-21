//! `/projects` and `/fetch`: list what a connected server holds, and adopt one project
//! (image + layout + colour + chat) as the session's working image.
const std = @import("std");
const image = @import("../../media/image.zig");
const server = @import("../../server/client.zig");
const logo = @import("../../app/logo.zig");
const project = @import("../../project.zig");
const msg = @import("../../app/messages.zig");
const ui = @import("../ui.zig");
const Session = @import("../session.zig").Session;
const projectsTable = @import("../render/projectsTable.zig");
const remoteEvents = @import("../remoteEvents.zig");

/// `/projects [url]` — an aligned table (NAME / SIZE / CHANGED, plus SERVER across more than
/// one). Bare lists every connected server's projects; a URL, just that one.
pub fn doProjects(session: *Session, io: std.Io, arg: []const u8) !void {
    if (session.servers.items.len == 0) {
        logo.print(msg.no_server_connections, .{});
        return;
    }
    const now = std.Io.Clock.real.now(io).toMilliseconds();

    var rows: std.ArrayList(projectsTable.ProjectRow) = .empty;
    defer projectsTable.freeRows(session.gpa, &rows);

    var multi = false;
    if (arg.len != 0) {
        const base = try server.normalizeBase(session.gpa, arg);
        defer session.gpa.free(base);
        const client = session.findServer(base) orelse {
            logo.print(msg.not_connected_connect_first, .{base});
            return;
        };
        try projectsTable.gatherRows(session.gpa, &rows, client, now, false);
        logo.print(msg.projects_on, .{ client.base, rows.items.len });
    } else {
        multi = session.servers.items.len > 1;
        for (session.servers.items) |*c| try projectsTable.gatherRows(session.gpa, &rows, c, now, multi);
        if (multi) {
            logo.print(msg.projects_across, .{ session.servers.items.len, rows.items.len });
        } else {
            logo.print(msg.projects_on, .{ session.servers.items[0].base, rows.items.len });
        }
    }

    if (rows.items.len == 0) {
        logo.print(msg.none_row, .{});
        return;
    }
    projectsTable.renderTable(session.gpa, rows.items, multi);
    logo.print(msg.fetch_hint, .{});
}

/// `/fetch <project name> [url]` — load a server project's image to continue editing. A
/// bare `/fetch` shows what there is to fetch: the projects table, plus the usage hint.
pub fn doFetch(session: *Session, io: std.Io, arg: []const u8) !void {
    if (arg.len == 0) {
        if (session.servers.items.len == 0) {
            logo.err(msg.no_connections, .{});
            return;
        }
        try doProjects(session, io, "");
        logo.print(msg.fetch_usage, .{});
        return;
    }
    var it = std.mem.tokenizeAny(u8, arg, " \t");
    const name = it.next().?;
    const url_arg = it.next();

    // Choose the server: a given URL, or the only connection.
    var client: *server.Client = undefined;
    if (url_arg) |u| {
        const base = try server.normalizeBase(session.gpa, u);
        defer session.gpa.free(base);
        client = session.findServer(base) orelse {
            logo.err(msg.not_connected_connect_first, .{base});
            return;
        };
    } else if (session.servers.items.len == 1) {
        client = &session.servers.items[0];
    } else if (session.servers.items.len == 0) {
        logo.err(msg.no_connections, .{});
        return;
    } else {
        logo.err(msg.fetch_needs_url, .{name});
        return;
    }

    const ref = (client.findProjectRef(name) catch |e| {
        logo.err(msg.server_lookup_failed, .{@errorName(e)});
        return;
    }) orelse {
        logo.err(msg.no_project_named_on, .{ name, client.base });
        return;
    };
    defer session.gpa.free(ref.id);

    const bytes = client.downloadFile(ref.id, "original") catch |e| {
        logo.err(msg.could_not_download_image, .{@errorName(e)});
        return;
    };
    defer session.gpa.free(bytes);
    const img = image.decode(session.gpa, bytes) catch |e| {
        logo.err(msg.could_not_decode_image, .{@errorName(e)});
        return;
    };
    try session.loadImage(img, name, true, .png, null);
    try session.setRemote(client.base, ref.id);
    session.remote_version = ref.version; // seed the LWW guard for live auto-pull
    // Adopt the project's custom name colour so the status header paints "<name>" in it.
    if (client.getProjectColor(ref.id)) |c| {
        defer session.gpa.free(c);
        session.setRemoteColor(c) catch {};
    } else |_| {
        session.setRemoteColor("") catch {};
    }
    remoteEvents.adoptServerLayout(session, client, ref.id); // show the project's stored crop/rotation/filter/lines
    remoteEvents.restoreServerChat(session, client, ref.id); // §12: restore the project's saved chat (only when /chat is on)
    // Open the live read-only events feed ALWAYS (not just when syncing) so a peer's name/colour
    // change updates the header even with sync off; sync only gates auto-pulling layout edits.
    session.openEvents(client);
    ui.redraw(session);
    logo.print(msg.fetched_sync, .{ name, client.base, if (session.sync) "on" else "off" });
}

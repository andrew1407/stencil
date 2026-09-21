//! `/rename`, `/expire`, `/sync` and `/chat`: the active project's identity, retention and
//! the two session-wide toggles that decide what is pushed to and restored from the server.
const std = @import("std");
const image = @import("../../media/image.zig");
const server = @import("../../server/client.zig");
const logo = @import("../../app/logo.zig");
const core = @import("../../core.zig");
const msg = @import("../../app/messages.zig");
const ui = @import("../ui.zig");
const Session = @import("../session.zig").Session;
const requireActiveProject = @import("projectMeta.zig").requireActiveProject;
const putProjectField = @import("projectMeta.zig").putProjectField;

/// `/rename <new name>` — rename the active fetched project, pushed live to the server
/// (version-guarded). Updates the displayed label + reprints the header.
pub fn doRename(session: *Session, arg: []const u8) !void {
    const active = requireActiveProject(session) orelse return;
    const name = std.mem.trim(u8, arg, " \t");
    if (name.len == 0) {
        logo.err(msg.rename_needs_name, .{});
        return;
    }
    if (!putProjectField(session, active.client, active.id, name, .name)) return;
    session.setLabel(name) catch {};
    logo.print(msg.renamed, .{name});
    ui.status(session); // reprint "image: <name> …" with the new name
}

/// `/expire [<duration>]` — the active project's expiry from a core-parsed duration ("days 23",
/// "month", "off"), resolved to now+duration and PUT version-guarded; "off" clears (0 = forever).
pub fn doExpire(session: *Session, io: std.Io, arg: []const u8) !void {
    const spec = std.mem.trim(u8, arg, " \t");
    if (spec.len == 0) {
        printExpireFormats();
        return;
    }
    const active = requireActiveProject(session) orelse return;
    const client = active.client;
    const ms = core.parseDuration(core.zstr(spec) orelse "") orelse {
        logo.err(msg.invalid_duration, .{spec});
        printExpireFormats();
        return;
    };
    const now = std.Io.Clock.real.now(io).toMilliseconds();
    const expires_at: i64 = if (ms == 0) 0 else now + ms;
    if (!putProjectExpiry(session, client, active.id, expires_at)) return; // message printed
    if (expires_at == 0) {
        logo.print(msg.expiry_cleared, .{});
    } else {
        var tb: [32]u8 = undefined;
        logo.print(msg.expires, .{server.formatUntil(&tb, now, expires_at)});
    }
}

/// Print the `/expire` formats (bare or bad arg); the word lists come from the core parser.
fn printExpireFormats() void {
    logo.print(msg.expire_usage, .{});
    logo.print(msg.expire_usage_unit, .{core.durationUnitsHelp()});
    logo.print(msg.expire_usage_count, .{});
    logo.print(msg.expire_usage_off, .{core.durationOffHelp()});
}

/// Version-guarded PUT of a project's expiry (epoch ms; 0 = keep forever) with a 409 retry,
/// mirroring putProjectField. Advances the LWW guard on success. Prints on a hard failure.
fn putProjectExpiry(session: *Session, client: *server.Client, id: []const u8, expires_at: i64) bool {
    var tries: u8 = 0;
    while (tries < 4) : (tries += 1) {
        client.updateProjectExpiry(id, expires_at, session.remote_version) catch |e| {
            if (e == server.Error.Conflict) {
                if (client.getProjectVersion(id)) |v| {
                    session.remote_version = v;
                    continue; // re-read won the race; retry the PUT
                } else |_| return false;
            }
            logo.err(msg.could_not_set_expiry, .{@errorName(e)});
            return false;
        };
        if (client.getProjectVersion(id)) |v| {
            session.remote_version = v;
        } else |_| {}
        return true;
    }
    return false;
}

/// `/sync [on|off]` — when on, every edit (and save) uploads the result to the active project;
/// a bare `/sync` (no argument) toggles the current state.
pub fn doSync(session: *Session, arg: []const u8) void {
    const a = std.mem.trim(u8, arg, " \t");
    if (std.ascii.eqlIgnoreCase(a, "on") or std.ascii.eqlIgnoreCase(a, "true")) {
        session.sync = true;
    } else if (std.ascii.eqlIgnoreCase(a, "off") or std.ascii.eqlIgnoreCase(a, "false")) {
        session.sync = false;
    } else if (a.len == 0) {
        session.sync = !session.sync; // bare /sync toggles
    } else {
        logo.err(msg.sync_usage, .{});
        return;
    }
    logo.print(msg.sync_state, .{if (session.sync) "on" else "off"});
    if (session.sync and session.remote_id == null)
        logo.print(msg.sync_no_project, .{});
    // The feed stays open whenever a project is active (a peer's name/colour change updates the
    // header with sync off too) — sync only gates auto-pulling layout edits. Open it here.
    if (session.events == null) {
        if (session.remote_url) |u| {
            if (session.findServer(u)) |client| session.openEvents(client);
        }
    }
}

/// `/chat [show|on|off|clear]` — opt-in per-project chat persistence (contract §12). on/off sets
/// whether the /prompt conversation rides the project; clear drops the turns and (§12.2) its file.
pub fn doChat(session: *Session, arg: []const u8) void {
    const a = std.mem.trim(u8, arg, " \t");
    const eq = std.ascii.eqlIgnoreCase;
    var turned_on = false;
    if (eq(a, "on") or eq(a, "true")) {
        session.chat_on = true;
        turned_on = true;
    } else if (eq(a, "off") or eq(a, "false")) {
        session.chat_on = false;
    } else if (eq(a, "clear")) {
        session.clearChat();
        logo.print(msg.chat_history_cleared, .{});
        // Clearing the conversation clears the persisted server copy too (idempotent DELETE,
        // best-effort — a miss never surfaces as an error).
        if (session.chat_on and session.hasRemote()) {
            if (session.findServer(session.remote_url.?)) |client| {
                if (client.deleteFile(session.remote_id.?, "chat")) {
                    logo.print(msg.chat_file_deleted, .{});
                } else |_| {}
            }
        }
        return;
    } else if (a.len != 0 and !eq(a, "show")) {
        logo.err(msg.chat_usage, .{});
        return;
    }
    logo.print(msg.chat_state, .{ if (session.chat_on) "on" else "off", session.chat_history.items.len });
    // §12.2: say who can read a saved chat BEFORE one is written anywhere.
    if (turned_on) {
        logo.print(msg.chat_on_note, .{});
    }
}

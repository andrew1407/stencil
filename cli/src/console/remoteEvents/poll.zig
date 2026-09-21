//! The live events feed: what a peer's edit means for the working image (pull it, warn
//! that local edits would be lost, or ignore our own echo), and the metadata refresh.
const std = @import("std");
const image = @import("../../media/image.zig");
const server = @import("../../server/client.zig");
const logo = @import("../../app/logo.zig");
const llm = @import("../../llm.zig");
const project = @import("../../project.zig");
const ui = @import("../ui.zig");
const Session = @import("../session.zig").Session;
const adoptServerLayout = @import("push.zig").adoptServerLayout;

/// What to do with one incoming project event for the active project. Kept pure (no I/O,
/// no session) so the live-edit decision is unit-tested without a socket or a server.
pub const PullAction = enum {
    ignore, // not our project, or an edit we already hold (incl. our own echoed push)
    pull, // a newer peer edit and no local edits pending — take it
    warn_dirty, // a newer peer edit but we have unsynced local edits — don't clobber, warn
    deleted, // the active project was deleted on the server
};

pub fn pullAction(remote_active: bool, ids_match: bool, deleted: bool, ev_version: i64, remote_version: i64, dirty: bool) PullAction {
    if (!remote_active or !ids_match) return .ignore;
    if (deleted) return .deleted;
    if (ev_version <= remote_version) return .ignore; // older, or our own push echoed back
    if (dirty) return .warn_dirty;
    return .pull;
}

/// Clear the current terminal line ONCE before emitting async output at the prompt (tracked via
/// `done`), so the caller only repaints — and a no-op poll prints nothing → no idle flicker.
pub fn clearPromptLine(done: *bool) void {
    if (done.*) return;
    logo.print("\r\x1b[K", .{});
    done.* = true;
}

/// Drain pending project events and act on ones touching the active project: auto-pull a peer's
/// newer edit, reflect name/colour changes, or warn rather than clobber unsynced local edits.
pub fn pollEvents(session: *Session, io: std.Io) bool {
    if (session.events == null) return false;
    const now = std.Io.Clock.real.now(io).toMilliseconds();
    var printed = false;
    while (session.events.?.poll() catch null) |ev| {
        var e = ev;
        defer e.deinit(session.gpa);
        const ids_match = session.remote_id != null and std.mem.eql(u8, e.id, session.remote_id.?);
        switch (pullAction(session.hasRemote(), ids_match, e.deleted, e.version, session.remote_version, session.dirty)) {
            .ignore => {},
            .deleted => {
                clearPromptLine(&printed);
                logo.print("🔴 \"{s}\" was deleted on the server\n", .{e.name});
            },
            .pull => {
                session.remote_version = e.version;
                if (session.sync) {
                    // Live editing: pull the peer's image + layout (also refreshes name + colour, reprints).
                    clearPromptLine(&printed);
                    pullActive(session, &e, now);
                } else if (applyMetaUpdate(session, e.name)) {
                    // Sync off: never pull image/layout edits, but always reflect a peer's NAME/COLOUR.
                    // Refresh the WHOLE view rather than stacking a new "image: …" line under the old one.
                    printed = true;
                    ui.redraw(session);
                }
            },
            .warn_dirty => {
                session.remote_version = e.version;
                _ = applyMetaUpdate(session, e.name); // metadata is cheap and clobbers nothing
                clearPromptLine(&printed);
                var tb: [32]u8 = undefined;
                logo.print(
                    "↺ \"{s}\" changed on the server ({s}) — you have local edits; '/save' to push yours or '/fetch' to take theirs\n",
                    .{ e.name, server.formatAgo(&tb, now, e.updated_at) },
                );
            },
        }
    }
    return printed;
}

/// Refresh the active project's displayed name + colour from a peer's metadata change; the colour is
/// re-read from the server. True when either actually changed, so the caller only then reprints.
pub fn applyMetaUpdate(session: *Session, name: []const u8) bool {
    var changed = false;
    if (name.len != 0 and (session.label == null or !std.mem.eql(u8, session.label.?, name))) {
        session.setLabel(name) catch {};
        changed = true;
    }
    const client = session.findServer(session.remote_url.?) orelse return changed;
    if (client.getProjectColor(session.remote_id.?)) |c| {
        defer session.gpa.free(c);
        const old = session.remote_color orelse "";
        if (!std.mem.eql(u8, old, c)) {
            session.setRemoteColor(c) catch {};
            changed = true;
        }
    } else |_| {}
    return changed;
}

/// Replace the working image with the active project's latest server image (a peer's edit). Resets
/// the undo history to it and clears the dirty flag — the session now matches the server.
pub fn pullActive(session: *Session, e: *const server.Event, now: i64) void {
    const client = session.findServer(session.remote_url.?) orelse return;
    // Pull the ORIGINAL + the layout and rebuild the view from them (rotate/crop/filter/lines),
    // the same way the GUIs reconstruct a peer's change — never the baked result.
    const bytes = client.downloadFile(session.remote_id.?, "original") catch |err| {
        logo.print("↺ \"{s}\" changed but the image could not be pulled ({s})\n", .{ e.name, @errorName(err) });
        return;
    };
    defer session.gpa.free(bytes);
    const img = image.decode(session.gpa, bytes) catch |err| {
        logo.print("↺ pull failed: could not decode the server image ({s})\n", .{@errorName(err)});
        return;
    };
    session.loadImage(img, e.name, true, session.default_fmt, null) catch |err| {
        logo.print("↺ pull failed ({s})\n", .{@errorName(err)});
        return;
    };
    adoptServerLayout(session, client, session.remote_id.?); // apply the peer's crop/rotation/filter/lines
    // A peer may also have recoloured the project — refresh so the header repaints in it.
    if (client.getProjectColor(session.remote_id.?)) |c| {
        defer session.gpa.free(c);
        session.setRemoteColor(c) catch {};
    } else |_| {}
    session.dirty = false; // the working image now matches the server
    var tb: [32]u8 = undefined;
    ui.redraw(session);
    logo.print("↺ pulled \"{s}\" from the server (changed {s})\n", .{ e.name, server.formatAgo(&tb, now, e.updated_at) });
}

//

const testing = std.testing;

test "pullAction: live-pull a newer peer edit, warn on local edits, ignore self/old" {
    // No active project, or an event for a different project → ignore.
    try testing.expectEqual(PullAction.ignore, pullAction(false, false, false, 5, 0, false));
    try testing.expectEqual(PullAction.ignore, pullAction(true, false, false, 5, 0, false));

    // A newer peer edit with no pending local edits → pull it.
    try testing.expectEqual(PullAction.pull, pullAction(true, true, false, 5, 4, false));

    // A newer peer edit but we have unsynced local edits → warn, don't clobber.
    try testing.expectEqual(PullAction.warn_dirty, pullAction(true, true, false, 5, 4, true));

    // Our own push echoed back (version not newer than what we hold) → ignore, even dirty.
    try testing.expectEqual(PullAction.ignore, pullAction(true, true, false, 4, 4, false));
    try testing.expectEqual(PullAction.ignore, pullAction(true, true, false, 3, 4, true));

    // A delete of the active project is surfaced regardless of version/dirty.
    try testing.expectEqual(PullAction.deleted, pullAction(true, true, true, 9, 4, true));
}

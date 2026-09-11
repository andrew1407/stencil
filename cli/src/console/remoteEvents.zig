//! Live server-project sync for the console: the events feed poll (peer pulls,
//! metadata refresh, dirty-warn), the /sync debounce, and the layout/result/chat
//! push + adopt helpers shared by /save, /fetch and the flush.
const std = @import("std");
const image = @import("../image.zig");
const server = @import("../serverClient.zig");
const logo = @import("../logo.zig");
const llm = @import("../llm.zig");
const project = @import("../project.zig");
const ui = @import("ui.zig");
const Session = @import("session.zig").Session;

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
fn clearPromptLine(done: *bool) void {
    if (done.*) return;
    logo.print("\r\x1b[K", .{});
    done.* = true;
}

/// Drain pending project events and act on ones touching the active project: auto-pull a
/// peer's newer edit, reflect name/colour changes, or warn instead of clobbering unsynced
/// local edits. Prompt-boundary, best-effort, never blocks. True if it printed anything.
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

/// Refresh the active project's displayed name + colour from a peer's metadata change. `name` is
/// the event's (canonical) name; the colour is re-read from the server. Returns true when either
/// actually changed (so the caller only reprints on a real change). Cheap — no image download.
fn applyMetaUpdate(session: *Session, name: []const u8) bool {
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

/// Replace the working image with the active project's latest server image (a peer's edit).
/// Resets the undo history to the pulled image and clears the dirty flag — the session now
/// matches the server. Keeps the active-remote/events binding intact.
fn pullActive(session: *Session, e: *const server.Event, now: i64) void {
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
// Each edit sets a cheap `dirty` flag (markDirty) and the REPL flushes once the input burst
// settles (flushSync, at the prompt boundary) — one upload per run of edits, not per action.

/// Queue a sync upload for the current result. Cheap and synchronous; the actual upload is
/// deferred to `flushSync`. No-op unless sync is on and a server project is active.
pub fn markDirty(session: *Session) void {
    if (session.sync and session.hasRemote()) session.dirty = true;
}

/// Pure debounce decision: upload only when sync is on, a project is active, an edit is
/// pending, and the input burst has settled (no more buffered commands). Unit-tested.
pub fn shouldFlush(sync: bool, has_remote: bool, dirty: bool, input_pending: bool) bool {
    return sync and has_remote and dirty and !input_pending;
}

/// Flush a pending sync upload when the burst has settled. `input_pending` is true when the
/// REPL still has buffered input to process, coalescing a run of edits into one upload.
pub fn flushSync(session: *Session, input_pending: bool) void {
    if (!shouldFlush(session.sync, session.hasRemote(), session.dirty, input_pending)) return;
    session.dirty = false;
    pushResult(session);
}

/// Push the current edit state to the active project: the structured LAYOUT (so CLI edits
/// show live in open GUI editors, which render original + layout), then the rendered
/// `result` raster (the projects-list thumbnail). Shared by the `/sync` flush and `/save`.
pub fn pushResult(session: *Session) void {
    if (!session.hasImage() or !session.hasRemote()) return;
    const client = session.findServer(session.remote_url.?) orelse return;
    const id = session.remote_id.?;
    pushLayout(session, client, id);
    const img = session.current();
    const result = image.encode(session.gpa, img.*, session.default_fmt) catch return;
    defer session.gpa.free(result);
    client.uploadFile(id, "result", result, session.default_fmt.ext(), img.width, img.height) catch |e| {
        logo.print("sync: upload failed ({s})\n", .{@errorName(e)});
        return;
    };
    pushChat(session, client, id); // §12: the persisted chat rides along when /chat is on
    // Advance the LWW guard to the version our push produced, so the server's echo of our own
    // change (which arrives on the events feed) isn't mistaken for a peer edit to pull.
    if (client.getProjectVersion(id)) |v| {
        session.remote_version = v;
    } else |_| {}
    logo.print("synced to {s}\n", .{client.base});
}

/// PUT the current structured layout (version-guarded). On a 409 (a peer saved first) re-read
/// the version and retry — last-writer-wins for the CLI's edits (a fetched project already
/// carries the server's lines/geometry, so a normal push preserves them).
fn pushLayout(session: *Session, client: *server.Client, id: []const u8) void {
    var tries: u8 = 0;
    while (tries < 4) : (tries += 1) {
        const layout = session.currentLayoutJson() catch return;
        defer session.gpa.free(layout);
        client.updateProject(id, layout, session.remote_version) catch |e| {
            if (e == server.Error.Conflict) {
                if (client.getProjectVersion(id)) |v| {
                    session.remote_version = v;
                    continue; // re-read won the race; retry the PUT
                } else |_| return;
            }
            logo.print("sync: layout update failed ({s})\n", .{@errorName(e)});
            return;
        };
        if (client.getProjectVersion(id)) |v| {
            session.remote_version = v;
        } else |_| {}
        return;
    }
}

/// With /chat on, fetch the project's persisted chat (§9 `chat` file kind) and adopt it as
/// the session history (§12: restoring seeds the replay history, never triggers a model
/// call). Best-effort: a missing or malformed document restores nothing, silently.
pub fn restoreServerChat(session: *Session, client: *server.Client, id: []const u8) void {
    if (!session.chat_on) return;
    const bytes = client.downloadFile(id, "chat") catch return;
    defer session.gpa.free(bytes);
    const turns = llm.parseChatDoc(session.gpa, bytes) catch return;
    if (turns.len == 0) return llm.freeTurns(session.gpa, turns);
    session.adoptChatTurns(turns);
    logo.print("restored {d} saved chat turns\n", .{session.chat_history.items.len});
}

/// With /chat on and turns saved, upload the §12.1 chat document alongside the pushed
/// project state (§9 `chat` kind, ext=json). Best-effort — never blocks the image push.
fn pushChat(session: *Session, client: *server.Client, id: []const u8) void {
    if (!session.chat_on or session.chat_history.items.len == 0) return;
    const saved_at = std.Io.Clock.real.now(client.io).toMilliseconds();
    const doc = llm.chatDocAlloc(session.gpa, session.chat_history.items, saved_at) catch return;
    defer session.gpa.free(doc);
    client.uploadFile(id, "chat", doc, "json", 0, 0) catch {};
}

/// Fetch the active project's stored layout and adopt it into the session (crop/rotation/filter/
/// lines), so a fetched/pulled project's full state shows — not just the bare original.
pub fn adoptServerLayout(session: *Session, client: *server.Client, id: []const u8) void {
    const body = client.getProject(id) catch return;
    defer session.gpa.free(body);
    const layout_json = extractLayoutObject(session.gpa, body) catch return;
    defer session.gpa.free(layout_json);
    session.adoptServerLayout(layout_json) catch {};
}

/// Pull the `layout` object out of a GET /projects/{id} response as its own JSON string ("{}"
/// when absent). Caller owns the result.
fn extractLayoutObject(gpa: std.mem.Allocator, body: []const u8) ![]u8 {
    var parsed = std.json.parseFromSlice(std.json.Value, gpa, body, .{}) catch return server.Error.BadResponse;
    defer parsed.deinit();
    if (parsed.value == .object) {
        if (parsed.value.object.get("layout")) |lv| {
            if (lv == .object) return std.json.Stringify.valueAlloc(gpa, lv, .{});
        }
    }
    return gpa.dupe(u8, "{}");
}

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

test "shouldFlush only uploads when on, active, dirty, and the burst has settled" {
    // The happy path: all preconditions met and no more buffered input.
    try testing.expect(shouldFlush(true, true, true, false));
    // Deferred while more commands are still queued (coalesce the burst into one upload).
    try testing.expect(!shouldFlush(true, true, true, true));
    // Each precondition is necessary.
    try testing.expect(!shouldFlush(false, true, true, false)); // sync off
    try testing.expect(!shouldFlush(true, false, true, false)); // no active project
    try testing.expect(!shouldFlush(true, true, false, false)); // nothing pending
}

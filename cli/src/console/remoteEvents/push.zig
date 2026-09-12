//! Pushing the session up: the /sync debounce (each edit marks dirty, the REPL flushes once
//! the input burst settles) and the layout / result / chat uploads /save, /fetch and the
//! flush share.
const std = @import("std");
const image = @import("../../image.zig");
const server = @import("../../serverClient.zig");
const logo = @import("../../logo.zig");
const llm = @import("../../llm.zig");
const project = @import("../../project.zig");
const ui = @import("../ui.zig");
const Session = @import("../session.zig").Session;
const poll = @import("poll.zig");

const clearPromptLine = poll.clearPromptLine;

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

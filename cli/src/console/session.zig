//! Console working-image state: the current RGBA8 image plus an undo/redo snapshot stack.
//! Each edit pushes a new state; `/undo`, `/redo` and `/reset` move a cursor over them. A
//! URL/blank/clipboard source is in-memory only and freed when replaced or on session end.
const std = @import("std");
const image = @import("../image.zig");
const server = @import("../server.zig");

const max_states = 64; // original + up to 63 undoable edits; older edits drop off the front

pub const Session = struct {
    gpa: std.mem.Allocator,
    label: ?[]u8 = null, // owned display label (the source path / URL / "blank" / "clipboard")
    temp: bool = false, // in-memory only (URL, blank, clipboard), not backed by a file on disk
    default_fmt: image.Format = .png,
    states: std.ArrayList(image.Rgba8) = .empty, // [0] = original; the current image is states[cursor]
    cursor: usize = 0,

    // ── Server connections (collaboration) ──
    servers: std.ArrayList(server.Client) = .empty, // connected servers (REST clients)
    sync: bool = false, // when on, edits auto-upload the result to the active remote
    dirty: bool = false, // a pending sync upload coalesced from a burst of edits (see handlers.flushSync)
    remote_url: ?[]u8 = null, // owned base URL of the active fetched project's server
    remote_id: ?[]u8 = null, // owned id of the active fetched project
    events: ?server.EditConn = null, // live read-only project-events feed (opened while syncing)
    events_url: ?[]u8 = null, // owned base URL the events feed is connected to

    /// True when a fetched server project is active (a target for sync / manual push).
    pub fn hasRemote(self: *const Session) bool {
        return self.remote_id != null and self.remote_url != null;
    }

    pub fn deinit(self: *Session) void {
        self.clearAll();
        self.closeEvents();
        self.states.deinit(self.gpa);
        for (self.servers.items) |*c| c.deinit();
        self.servers.deinit(self.gpa);
        self.clearRemote();
    }

    // ── live events feed ──
    /// Open (or replace) the read-only project-events subscription to `client`'s server.
    /// Best-effort: a failed connect leaves the feed closed and is not fatal.
    pub fn openEvents(self: *Session, client: *server.Client) void {
        self.closeEvents();
        const conn = server.EditConn.open(self.gpa, client.io, client.base, client.token, "stencil-cli") catch return;
        const url = self.gpa.dupe(u8, client.base) catch {
            var c = conn;
            c.deinit();
            return;
        };
        self.events = conn;
        self.events_url = url;
    }

    pub fn closeEvents(self: *Session) void {
        if (self.events) |*e| e.deinit();
        self.events = null;
        if (self.events_url) |u| self.gpa.free(u);
        self.events_url = null;
    }

    // ── connection helpers ──
    pub fn findServer(self: *Session, url: []const u8) ?*server.Client {
        for (self.servers.items) |*c| {
            if (std.mem.eql(u8, c.base, url)) return c;
        }
        return null;
    }

    pub fn dropServer(self: *Session, url: []const u8) bool {
        for (self.servers.items, 0..) |*c, i| {
            if (std.mem.eql(u8, c.base, url)) {
                if (self.events_url != null and std.mem.eql(u8, self.events_url.?, url)) self.closeEvents();
                c.deinit();
                _ = self.servers.orderedRemove(i);
                return true;
            }
        }
        return false;
    }

    /// Record the active remote project (owns copies of url + id).
    pub fn setRemote(self: *Session, url: []const u8, id: []const u8) !void {
        const u = try self.gpa.dupe(u8, url);
        errdefer self.gpa.free(u);
        const i = try self.gpa.dupe(u8, id);
        self.clearRemote();
        self.remote_url = u;
        self.remote_id = i;
    }

    pub fn clearRemote(self: *Session) void {
        if (self.remote_url) |u| self.gpa.free(u);
        if (self.remote_id) |i| self.gpa.free(i);
        self.remote_url = null;
        self.remote_id = null;
    }

    pub fn hasImage(self: *Session) bool {
        return self.states.items.len != 0;
    }

    pub fn current(self: *Session) *image.Rgba8 {
        return &self.states.items[self.cursor];
    }

    /// A writable copy of the current image, for a transform to mutate before `commit`.
    pub fn workCopy(self: *Session) !image.Rgba8 {
        const cur = self.current();
        return .{ .width = cur.width, .height = cur.height, .pixels = try self.gpa.dupe(u8, cur.pixels) };
    }

    /// Replace the whole session with a freshly loaded source (its own fresh history).
    pub fn loadImage(self: *Session, img: image.Rgba8, label: []const u8, temp: bool, fmt: image.Format) !void {
        const dup = self.gpa.dupe(u8, label) catch |e| return freeImg(self.gpa, img, e);
        self.clearAll();
        self.states.append(self.gpa, img) catch |e| {
            self.gpa.free(dup);
            return freeImg(self.gpa, img, e);
        };
        self.label = dup;
        self.temp = temp;
        self.default_fmt = fmt;
        self.cursor = 0;
    }

    /// Make `work` the new current state, discarding any redo states ahead of the cursor.
    pub fn commit(self: *Session, work: image.Rgba8) !void {
        self.dropAfterCursor();
        self.states.append(self.gpa, work) catch |e| return freeImg(self.gpa, work, e);
        self.cursor = self.states.items.len - 1;
        // Bound memory: drop the oldest *edit* (never the original) once history is too deep.
        while (self.states.items.len > max_states) {
            self.states.items[1].deinit(self.gpa);
            _ = self.states.orderedRemove(1);
            self.cursor -= 1;
        }
    }

    pub fn undo(self: *Session) bool {
        if (self.cursor == 0) return false;
        self.cursor -= 1;
        return true;
    }

    pub fn redo(self: *Session) bool {
        if (self.cursor + 1 >= self.states.items.len) return false;
        self.cursor += 1;
        return true;
    }

    /// Revert to the original source, dropping every edit and the redo history.
    pub fn revert(self: *Session) void {
        self.cursor = 0;
        self.dropAfterCursor();
    }

    fn dropAfterCursor(self: *Session) void {
        var i = self.states.items.len;
        while (i > self.cursor + 1) : (i -= 1) self.states.items[i - 1].deinit(self.gpa);
        self.states.shrinkRetainingCapacity(self.cursor + 1);
    }

    pub fn clearAll(self: *Session) void {
        for (self.states.items) |*st| st.deinit(self.gpa);
        self.states.clearRetainingCapacity();
        self.cursor = 0;
        if (self.label) |l| self.gpa.free(l);
        self.label = null;
        self.temp = false;
        self.default_fmt = .png;
    }
};

fn freeImg(gpa: std.mem.Allocator, img: image.Rgba8, e: anyerror) anyerror {
    var m = img;
    m.deinit(gpa);
    return e;
}

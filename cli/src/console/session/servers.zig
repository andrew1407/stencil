//! The session's collaboration-server pool: which servers are connected, which project is
//! active, and the live edit-events feed. Only URLs the user typed ever enter the pool.
const Session = @import("../session.zig").Session;
const std = @import("std");
const server = @import("../../serverClient.zig");
const llm = @import("../../llm.zig");


/// True when a fetched server project is active (a target for sync / manual push).
pub fn hasRemote(self: *const Session) bool {
    return self.remote_id != null and self.remote_url != null;
}

/// The session's LLM configuration, resolved from the captured environment on first use
/// (so `/llm` overrides layer on top of the `STENCIL_LLM_*` initial values).
pub fn llmConfig(self: *Session) !*llm.Config {
    if (self.llm_cfg == null) self.llm_cfg = try llm.Config.init(self.gpa, self.llm_env);
    return &self.llm_cfg.?;
}

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

/// Remember a successfully connected base URL in the known-servers pool (deduped).
pub fn rememberServer(self: *Session, base: []const u8) !void {
    for (self.known_servers.items) |u| {
        if (std.mem.eql(u8, u, base)) return;
    }
    const dup = try self.gpa.dupe(u8, base);
    errdefer self.gpa.free(dup);
    try self.known_servers.append(self.gpa, dup);
}

pub fn findServer(self: *Session, url: []const u8) ?*server.Client {
    for (self.servers.items) |*c| {
        if (std.mem.eql(u8, c.base, url)) return c;
    }
    return null;
}

/// Index of a connected server by base URL, for in-place replacement (`/reconnect`).
pub fn indexOfServer(self: *Session, url: []const u8) ?usize {
    for (self.servers.items, 0..) |*c, i| {
        if (std.mem.eql(u8, c.base, url)) return i;
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

/// Set the active project's custom name colour ("#rrggbb" or "" to clear), owned copy.
pub fn setRemoteColor(self: *Session, color: []const u8) !void {
    const c = try self.gpa.dupe(u8, color);
    if (self.remote_color) |old| self.gpa.free(old);
    self.remote_color = c;
}

pub fn clearRemote(self: *Session) void {
    if (self.remote_url) |u| self.gpa.free(u);
    if (self.remote_id) |i| self.gpa.free(i);
    if (self.remote_color) |c| self.gpa.free(c);
    self.remote_url = null;
    self.remote_id = null;
    self.remote_color = null;
    self.remote_version = 0;
}

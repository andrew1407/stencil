//! The live edit channel: newline-delimited frames over raw TCP, the `updated` events a
//! peer's save produces, and the buffered connection that reassembles partial frames.
const std = @import("std");
const builtin = @import("builtin");
const net = @import("../net.zig");
const Error = @import("errors.zig").Error;
const urls = @import("urls.zig");
const hostAndPort = urls.hostAndPort;
const editPort = urls.editPort;

/// Build one NDJSON frame: compact JSON + '\n'. Compact JSON never contains a raw
/// newline, so '\n' is an unambiguous delimiter for the TCP edit transport.
pub fn frame(gpa: std.mem.Allocator, json: []const u8) ![]u8 {
    return std.fmt.allocPrint(gpa, "{s}\n", .{json});
}

/// Build the hello frame that opens a TCP/WS edit session (empty project_id selects
/// the global events feed). Caller owns the returned slice.
pub fn helloFrame(gpa: std.mem.Allocator, token: []const u8, project_id: []const u8, client_id: []const u8) ![]u8 {
    return std.fmt.allocPrint(
        gpa,
        "{{\"type\":\"hello\",\"token\":\"{s}\",\"projectId\":\"{s}\",\"clientId\":\"{s}\"}}\n",
        .{ token, project_id, client_id },
    );
}

/// A parsed project-update event from the global feed (caller owns id + name). `version` is the server's
/// monotonic edit counter, used as the pull guard and never shown; `deleted` marks a delete event.
pub const Event = struct {
    id: []u8,
    name: []u8,
    version: i64,
    updated_at: i64 = 0,
    deleted: bool = false,
    pub fn deinit(self: *Event, gpa: std.mem.Allocator) void {
        gpa.free(self.id);
        gpa.free(self.name);
    }
};

/// Parse one NDJSON frame: returns an owned project-update event, or null for any other
/// frame type / parse failure. Pure — unit-tested without a socket.
pub fn parseEvent(gpa: std.mem.Allocator, json: []const u8) !?Event {
    const T = struct {
        type: []const u8 = "",
        event: []const u8 = "",
        project: ?struct {
            id: []const u8 = "",
            name: []const u8 = "",
            version: i64 = 0,
            updatedAt: i64 = 0,
        } = null,
    };
    var p = std.json.parseFromSlice(T, gpa, json, .{ .ignore_unknown_fields = true }) catch return null;
    defer p.deinit();
    if (!std.mem.eql(u8, p.value.type, "project-event")) return null;
    const proj = p.value.project orelse return null;
    const id = try gpa.dupe(u8, proj.id);
    errdefer gpa.free(id);
    const name = try gpa.dupe(u8, proj.name);
    return Event{
        .id = id,
        .name = name,
        .version = proj.version,
        .updated_at = proj.updatedAt,
        .deleted = std.mem.eql(u8, p.value.event, "deleted"),
    };
}

/// A read-only subscription to a server's global project-events feed over the raw-TCP
/// edit channel. Connects, sends a hello, and drains "updated" events without blocking.
pub const EditConn = struct {
    gpa: std.mem.Allocator,
    io: std.Io,
    stream: std.Io.net.Stream,
    rbuf: std.ArrayList(u8) = .empty,
    closed: bool = false,

    /// Connect to the edit port, authenticate with a hello (empty projectId = global feed),
    /// and set the receive timeout that backs the non-blocking drain below.
    pub fn open(gpa: std.mem.Allocator, io: std.Io, base: []const u8, token: []const u8, client_id: []const u8) !EditConn {
        // The events feed is a plaintext TCP socket and cannot speak TLS, so over https we skip the live feed
        // rather than dial the wrong port. REST and sync still work over TLS.
        if (std.ascii.startsWithIgnoreCase(base, "https://")) return Error.TlsNotSupported;
        const hp = hostAndPort(base);
        // Judged under serverTarget as the REST exchanges are, and dialled at an address judged.
        var stream = try net.dialServer(io, hp.host, editPort(hp.port));
        errdefer stream.close(io);
        // A read that reaches the socket is always one poll() said was ready, but a stalled peer
        // must not hold the prompt either: 100ms is the ceiling on any read that slips through.
        const tv = std.posix.timeval{ .sec = 0, .usec = 100 * 1000 };
        std.posix.setsockopt(stream.socket.handle, std.posix.SOL.SOCKET, std.posix.SO.RCVTIMEO, std.mem.asBytes(&tv)) catch {};
        const hello = try helloFrame(gpa, token, "", client_id);
        defer gpa.free(hello);
        var wbuf: [256]u8 = undefined;
        var sw = stream.writer(io, &wbuf);
        try sw.interface.writeAll(hello);
        try sw.interface.flush();
        return .{ .gpa = gpa, .io = io, .stream = stream };
    }

    pub fn deinit(self: *EditConn) void {
        self.stream.close(self.io);
        self.rbuf.deinit(self.gpa);
    }

    /// Best-effort drain: read whatever is already pending, then pop the next complete
    /// project-update event, or null. Repeated calls drain the buffer one event at a time.
    /// It NEVER blocks — the console calls it at every prompt boundary, piped runs included.
    pub fn poll(self: *EditConn) !?Event {
        if (self.closed) return null;
        if (std.mem.indexOfScalar(u8, self.rbuf.items, '\n') == null) {
            if (!readable(self.stream.socket.handle)) return null; // nothing pending right now
            var tmp: [4096]u8 = undefined;
            const n = std.posix.read(self.stream.socket.handle, &tmp) catch |e| switch (e) {
                error.WouldBlock => return null, // receive timeout: nothing pending right now
                else => {
                    self.closed = true;
                    return null;
                },
            };
            if (n == 0) {
                self.closed = true;
                return null;
            }
            try self.feed(tmp[0..n]);
        }
        return self.nextEvent();
    }

    /// Whether a read would return at once. Windows has no posix poll(), so there the
    /// receive timeout is what bounds the drain instead.
    fn readable(fd: std.posix.socket_t) bool {
        if (builtin.os.tag != .windows) {
            var fds = [_]std.posix.pollfd{.{ .fd = fd, .events = std.posix.POLL.IN, .revents = 0 }};
            return (std.posix.poll(&fds, 0) catch 0) != 0;
        }
        return true;
    }

    /// Append freshly-read socket bytes to the frame buffer. Split out for testing.
    pub fn feed(self: *EditConn, data: []const u8) !void {
        try self.rbuf.appendSlice(self.gpa, data);
    }

    /// Pop and parse complete NDJSON frames from the buffer, returning the next project-update event
    /// (skipping welcome/synced frames) or null. Pure buffer work — no socket — so it is unit-tested.
    pub fn nextEvent(self: *EditConn) !?Event {
        while (std.mem.indexOfScalar(u8, self.rbuf.items, '\n')) |nl| {
            const line = try self.gpa.dupe(u8, self.rbuf.items[0..nl]);
            defer self.gpa.free(line);
            const rest = self.rbuf.items[nl + 1 ..];
            std.mem.copyForwards(u8, self.rbuf.items, rest);
            self.rbuf.shrinkRetainingCapacity(rest.len);
            if (try parseEvent(self.gpa, line)) |ev| return ev;
        }
        return null;
    }
};

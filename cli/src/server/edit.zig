//! The live edit channel: newline-delimited frames over raw TCP, the `updated` events a
//! peer's save produces, and the buffered connection that reassembles partial frames.
const std = @import("std");
const Error = @import("errors.zig").Error;
const urls = @import("urls.zig");
const hostAndPort = urls.hostAndPort;
const editPort = urls.editPort;
const testing = std.testing;

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

/// A parsed project-update event from the global feed. Caller owns id + name.
/// `version` is the server's monotonic edit counter (used internally as the pull guard,
/// never shown to the user); `updated_at` is the change's epoch-ms timestamp (0 when the
/// frame omits it); `deleted` is true for a "deleted" event rather than an edit.
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

    /// Connect to the edit port, authenticate with a hello (empty projectId = global
    /// feed), and bound reads with a short receive timeout so draining never stalls.
    pub fn open(gpa: std.mem.Allocator, io: std.Io, base: []const u8, token: []const u8, client_id: []const u8) !EditConn {
        // The events feed is a plaintext TCP socket; it can't speak TLS, so when the
        // server is reached over https (its edit channel is TLS-wrapped too) we skip
        // the live feed rather than dial the wrong port. REST/sync still work over TLS.
        if (std.ascii.startsWithIgnoreCase(base, "https://")) return Error.TlsNotSupported;
        const hp = hostAndPort(base);
        const port = editPort(hp.port);
        // IpAddress.resolve only parses IP LITERALS (it ParseFails on a hostname like
        // "localhost"), so fall back to a DNS lookup via HostName for names. Without this the
        // events feed silently never opened for the common localhost server.
        var stream = if (std.Io.net.IpAddress.resolve(io, hp.host, port)) |lit| s: {
            var addr = lit;
            break :s try addr.connect(io, .{ .mode = .stream });
        } else |_| s: {
            const hn = try std.Io.net.HostName.init(hp.host);
            break :s try hn.connect(io, port, .{ .mode = .stream });
        };
        errdefer stream.close(io);
        const fd = stream.socket.handle;
        // 100ms receive timeout: a drain returns promptly (EAGAIN) when no events pend.
        const tv = std.posix.timeval{ .sec = 0, .usec = 100 * 1000 };
        std.posix.setsockopt(fd, std.posix.SOL.SOCKET, std.posix.SO.RCVTIMEO, std.mem.asBytes(&tv)) catch {};
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

    /// Best-effort drain: read whatever is pending (bounded by the receive timeout), then
    /// pop and return the next complete project-update event, or null when none remain.
    /// Repeated calls drain the buffer one event at a time.
    pub fn poll(self: *EditConn) !?Event {
        if (self.closed) return null;
        if (std.mem.indexOfScalar(u8, self.rbuf.items, '\n') == null) {
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

    /// Append freshly-read socket bytes to the frame buffer. Split out for testing.
    fn feed(self: *EditConn, data: []const u8) !void {
        try self.rbuf.appendSlice(self.gpa, data);
    }

    /// Pop and parse complete NDJSON frames from the buffer, returning the next project
    /// update event (skipping welcome/synced/other frames), or null when none remain.
    /// Pure buffer work — no socket — so the partial/multi-frame handling is unit-tested.
    fn nextEvent(self: *EditConn) !?Event {
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

test "helloFrame and frame are newline-delimited" {
    const a = testing.allocator;
    const h = try helloFrame(a, "tkn", "p_a_b", "c1");
    defer a.free(h);
    try testing.expect(h[h.len - 1] == '\n');
    try testing.expect(std.mem.indexOf(u8, h, "\"projectId\":\"p_a_b\"") != null);
    try testing.expect(std.mem.indexOf(u8, h, "\"token\":\"tkn\"") != null);

    const f = try frame(a, "{\"type\":\"ping\"}");
    defer a.free(f);
    try testing.expectEqualStrings("{\"type\":\"ping\"}\n", f);
}

test "parseEvent returns updated project-events, ignores other frames" {
    const a = testing.allocator;

    // A project-event frame yields an owned id/name/version + the change timestamp.
    const body = "{\"type\":\"project-event\",\"event\":\"updated\",\"project\":{\"id\":\"p_x_y\",\"name\":\"Notes\",\"version\":7,\"updatedAt\":1700000000000}}";
    var ev = (try parseEvent(a, body)).?;
    defer ev.deinit(a);
    try testing.expectEqualStrings("p_x_y", ev.id);
    try testing.expectEqualStrings("Notes", ev.name);
    try testing.expectEqual(@as(i64, 7), ev.version);
    try testing.expectEqual(@as(i64, 1700000000000), ev.updated_at);
    try testing.expect(!ev.deleted);

    // A "deleted" event is flagged; updatedAt absent defaults to 0.
    var del = (try parseEvent(a, "{\"type\":\"project-event\",\"event\":\"deleted\",\"project\":{\"id\":\"p_z\",\"name\":\"Gone\",\"version\":3}}")).?;
    defer del.deinit(a);
    try testing.expect(del.deleted);
    try testing.expectEqual(@as(i64, 0), del.updated_at);

    // Non-event frames (welcome, synced, hello echoes) are ignored.
    try testing.expect((try parseEvent(a, "{\"type\":\"welcome\",\"version\":1}")) == null);
    try testing.expect((try parseEvent(a, "{\"type\":\"project-event\"}")) == null); // no project
    try testing.expect((try parseEvent(a, "not json")) == null);
}

test "EditConn frame buffer handles partial, multiple, and skipped frames" {
    const a = testing.allocator;
    // io/stream are unused by feed/nextEvent (pure buffer work), so leave them undefined.
    var c = EditConn{ .gpa = a, .io = undefined, .stream = undefined };
    defer c.rbuf.deinit(a);

    // A frame split across two reads yields nothing until the newline arrives.
    try c.feed("{\"type\":\"project-event\",\"event\":\"updated\",\"project\":{\"id\":\"p1\",\"nam");
    try testing.expect((try c.nextEvent()) == null);

    // Completing it, plus a non-event frame and a second event, all in one chunk.
    try c.feed("e\":\"A\",\"version\":2}}\n{\"type\":\"welcome\",\"version\":1}\n" ++
        "{\"type\":\"project-event\",\"event\":\"updated\",\"project\":{\"id\":\"p2\",\"name\":\"B\",\"version\":5}}\n");

    var e1 = (try c.nextEvent()).?;
    defer e1.deinit(a);
    try testing.expectEqualStrings("p1", e1.id);
    try testing.expectEqual(@as(i64, 2), e1.version);

    // The welcome frame is skipped; the next event is p2.
    var e2 = (try c.nextEvent()).?;
    defer e2.deinit(a);
    try testing.expectEqualStrings("p2", e2.id);
    try testing.expectEqual(@as(i64, 5), e2.version);

    // Buffer drained.
    try testing.expect((try c.nextEvent()) == null);
}

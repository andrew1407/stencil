//! The console's network calls run on a worker while the terminal is watched: a Ctrl-C ends a
//! /connect handshake, the events feed's opening, a sync push, a peer pull, a metadata refresh, a
//! /fetch, an /upload of a URL or a /scrape instead of the console sitting deaf for the request's
//! whole deadline. The input loop installs the watch for its thread (net/job.zig).
const std = @import("std");
const net = @import("../net.zig");
const job = @import("../net/job.zig");
const jobCall = @import("../net/jobCall.zig");
const server = @import("../server/client.zig");

/// Put `w` in force (null = none: a call blocks, as a one-shot run's do); returns the one it replaces.
pub fn install(w: ?net.Waiter) ?net.Waiter {
    return job.watchThread(w);
}

/// The `server.Transport` a console client runs on.
pub fn transport(
    gpa: std.mem.Allocator,
    io: std.Io,
    url: []const u8,
    method: std.http.Method,
    payload: ?[]const u8,
    headers: []const std.http.Header,
) server.TransportError![]u8 {
    return server.watchedRequest(gpa, io, url, method, payload, headers, job.threadWatch());
}

/// Open `client`'s live events feed under the same watch; null when it failed or a Ctrl-C ended
/// the wait (a feed that opened just then is closed again).
pub fn openEvents(gpa: std.mem.Allocator, client: *const server.Client) ?server.EditConn {
    return switch (jobCall.call(client.io, null, server.EditConn.open, .{ gpa, client.io, client.base, client.token, "stencil-cli" })) {
        .done => |r| r catch null,
        .cancelled, .timed_out => |r| {
            var conn = r catch return null;
            conn.deinit();
            return null;
        },
    };
}

const testing = std.testing;
const logo = @import("../app/logo.zig");
const handlers = @import("handlers.zig");
const Session = @import("session.zig").Session;

const Presses = struct {
    left: usize,
    fn poll(ctx: *anyopaque, _: i32) bool {
        const self: *Presses = @ptrCast(@alignCast(ctx));
        if (self.left == 0) return true;
        self.left -= 1;
        return false;
    }
};

test "install hands back the watch it replaces" {
    var presses = Presses{ .left = 0 };
    const prev = install(.{ .ctx = &presses, .poll = Presses.poll });
    try testing.expect(prev == null);
    try testing.expect(install(prev) != null);
    try testing.expect(job.threadWatch() == null);
}

test "under the watch a /connect handshake to a server that never answers ends on a Ctrl-C" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var port: u16 = 40011; // a listener nobody accepts on: the handshake goes out, no answer comes
    var listener = while (port < 40100) : (port += 1) {
        var addr = std.Io.net.IpAddress.parse("127.0.0.1", port) catch unreachable;
        break addr.listen(io, .{ .reuse_address = true }) catch continue;
    } else return error.NoFreePort;
    defer listener.deinit(io);
    var url_buf: [64]u8 = undefined;
    const url = try std.fmt.bufPrint(&url_buf, "http://127.0.0.1:{d}", .{port});
    var presses = Presses{ .left = 2 };
    const prev = install(.{ .ctx = &presses, .poll = Presses.poll });
    defer _ = install(prev);
    const t0 = std.Io.Clock.now(.awake, io).toMilliseconds();
    try testing.expectError(server.Error.Cancelled, server.connect(testing.allocator, io, url, "tok"));
    try testing.expect(std.Io.Clock.now(.awake, io).toMilliseconds() - t0 < 5_000); // not the 30 s deadline
}

var base_io: std.Io = undefined;

// A dial that never completes: it naps until cancelled, as a SYN into a black hole would.
fn stalledConnect(_: ?*anyopaque, _: *const std.Io.net.IpAddress, _: std.Io.net.IpAddress.ConnectOptions) std.Io.net.IpAddress.ConnectError!std.Io.net.Socket {
    try base_io.sleep(.fromSeconds(20), .awake);
    return error.ConnectionRefused;
}

test "under the watch opening the events feed to a stalled server ends on a Ctrl-C" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    base_io = threaded.io();
    var vt = base_io.vtable.*;
    vt.netConnectIp = stalledConnect;
    const io: std.Io = .{ .userdata = base_io.userdata, .vtable = &vt };
    const a = testing.allocator;
    const client = server.Client{ .gpa = a, .io = io, .base = @constCast("http://127.0.0.1:8090"), .token = @constCast("t"), .auth = @constCast(""), .credential = @constCast("") };
    var presses = Presses{ .left = 2 };
    const prev = install(.{ .ctx = &presses, .poll = Presses.poll });
    defer _ = install(prev);
    const t0 = std.Io.Clock.now(.awake, base_io).toMilliseconds();
    try testing.expect(openEvents(a, &client) == null);
    try testing.expect(std.Io.Clock.now(.awake, base_io).toMilliseconds() - t0 < 5_000); // not the 20 s stall
    try testing.expectEqual(@as(usize, 0), presses.left);
}

const Said = struct {
    buf: [2048]u8 = undefined,
    len: usize = 0,
    fn take(ctx: *anyopaque, chunk: []const u8) void {
        const self: *Said = @ptrCast(@alignCast(ctx));
        const n = @min(chunk.len, self.buf.len - self.len);
        @memcpy(self.buf[self.len..][0..n], chunk[0..n]);
        self.len += n;
    }
};

/// `command(session, io, "http://127.0.0.1:<silent port>/<path>")` under a watch that presses
/// Ctrl-C on its third beat: over well before the fetch's 30 s deadline, nothing loaded, and said.
fn expectCancelledFetch(comptime command: anytype, path: []const u8) !void {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var port: u16 = 40111; // a listener nobody accepts on: the request goes out, no answer comes
    var listener = while (port < 40200) : (port += 1) {
        var addr = std.Io.net.IpAddress.parse("127.0.0.1", port) catch unreachable;
        break addr.listen(io, .{ .reuse_address = true }) catch continue;
    } else return error.NoFreePort;
    defer listener.deinit(io);
    var url_buf: [64]u8 = undefined;
    const url = try std.fmt.bufPrint(&url_buf, "http://127.0.0.1:{d}/{s}", .{ port, path });
    var session = Session{ .gpa = a };
    defer session.deinit();
    var said = Said{};
    logo.setSink(Said.take, &said);
    defer logo.clearSink();
    var presses = Presses{ .left = 2 };
    const prev = install(.{ .ctx = &presses, .poll = Presses.poll });
    defer _ = install(prev);
    const t0 = std.Io.Clock.now(.awake, io).toMilliseconds();
    try command(&session, io, url);
    try testing.expect(std.Io.Clock.now(.awake, io).toMilliseconds() - t0 < 5_000);
    try testing.expect(!session.hasImage());
    try testing.expect(std.mem.indexOf(u8, said.buf[0..said.len], "cancelled — ") != null);
}

test "under the watch an /upload of a URL that never answers ends on a Ctrl-C" {
    try expectCancelledFetch(handlers.doUpload, "x.png");
}

test "under the watch a /scrape of a page that never answers ends on a Ctrl-C" {
    try expectCancelledFetch(handlers.doSourceUpload, "page");
}

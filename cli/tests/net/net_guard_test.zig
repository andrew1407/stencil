// The guard every outbound request inherits, driven against a real loopback server through
// serverClient.rawRequest — the server-connect path, which skips only the host check
// (net.RequestOptions.allow_named_host) and must still cap the body and refuse a redirect.
const std = @import("std");
const net = @import("../../src/net.zig");
const server = @import("../../src/server/client.zig");
const testing = std.testing;

test "a response over the fetch cap is refused, not buffered" {
    var fake = try Fake.start("HTTP/1.1 200 OK\r\nContent-Length: 134217728\r\n\r\n", .flood);
    defer fake.stop();
    const url = try fake.url(testing.allocator);
    defer testing.allocator.free(url);

    try testing.expectError(error.HttpFailed, server.rawRequest(testing.allocator, fake.io, url, .GET, null, &.{}));
    try testing.expectEqual(@as(u32, 1), fake.hits.load(.acquire));
}

test "a redirect is refused, and the target is never fetched" {
    // The classic pivot: a server the user named 302s to the cloud-metadata address.
    var fake = try Fake.start("HTTP/1.1 302 Found\r\nLocation: http://169.254.169.254/latest/meta-data/\r\nContent-Length: 0\r\n\r\n", .plain);
    defer fake.stop();
    const url = try fake.url(testing.allocator);
    defer testing.allocator.free(url);

    try testing.expectError(error.HttpFailed, server.rawRequest(testing.allocator, fake.io, url, .GET, null, &.{}));
    try testing.expectEqual(@as(u32, 1), fake.hits.load(.acquire)); // one hop only
    try testing.expect(server.lastReject() == null); // never reached a status we could record
}

test "the host guard still applies to everything except the server-connect path" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const opts = net.RequestOptions{};
    try testing.expectError(error.BlockedHost, net.request(testing.allocator, io, "http://169.254.169.254/latest/meta-data/", opts));
}

test "a host that never answers is dropped at the deadline, not waited on" {
    var fake = try Fake.start("", .stall);
    defer fake.stop();
    const url = try fake.url(testing.allocator);
    defer testing.allocator.free(url);

    // A named server (the host check is skipped) that accepts and then says nothing.
    const opts = net.RequestOptions{ .allow_named_host = true, .timeout_ms = 300 };
    const started = std.Io.Clock.now(.awake, fake.io).toMilliseconds();
    try testing.expectError(error.TimedOut, net.request(testing.allocator, fake.io, url, opts));
    const waited = std.Io.Clock.now(.awake, fake.io).toMilliseconds() - started;
    try testing.expect(waited >= 300); // it really waited its budget …
    try testing.expect(waited < 10_000); // … and gave up on it rather than on the server
    try testing.expectEqual(@as(u32, 1), fake.hits.load(.acquire));
}

/// A one-request HTTP server on loopback. `.plain` answers with `head`; `.flood` then pours
/// bytes to run past the 64 MiB cap; `.stall` accepts the request and never answers at all.
const Fake = struct {
    const Mode = enum { plain, flood, stall };

    threaded: *std.Io.Threaded,
    listener: std.Io.net.Server,
    io: std.Io,
    port: u16,
    head: []const u8,
    mode: Mode,
    hits: std.atomic.Value(u32) = .init(0),
    stopping: std.atomic.Value(bool) = .init(false),
    thread: std.Thread = undefined,

    fn start(head: []const u8, mode: Mode) !*Fake {
        const a = testing.allocator;
        const threaded = try a.create(std.Io.Threaded);
        threaded.* = std.Io.Threaded.init(a, .{});
        const io = threaded.io();
        const self = try a.create(Fake);
        var port: u16 = 39871;
        self.* = while (port < 39931) : (port += 1) {
            var addr = std.Io.net.IpAddress.parse("127.0.0.1", port) catch unreachable;
            const l = addr.listen(io, .{ .reuse_address = true }) catch continue;
            break .{ .threaded = threaded, .listener = l, .io = io, .port = port, .head = head, .mode = mode };
        } else return error.NoFreePort;
        self.thread = try std.Thread.spawn(.{}, serve, .{self});
        return self;
    }

    fn stop(self: *Fake) void {
        const a = testing.allocator;
        self.stopping.store(true, .release); // ends a stalling serve loop
        self.listener.deinit(self.io); // unblocks the accept, ending the thread
        self.thread.join();
        self.threaded.deinit();
        a.destroy(self.threaded);
        a.destroy(self);
    }

    fn url(self: *Fake, a: std.mem.Allocator) ![]u8 {
        return std.fmt.allocPrint(a, "http://127.0.0.1:{d}/x", .{self.port});
    }

    fn serve(self: *Fake) void {
        const stream = self.listener.accept(self.io) catch return;
        defer stream.close(self.io);
        _ = self.hits.fetchAdd(1, .release);

        var rbuf: [4096]u8 = undefined;
        var reader = stream.reader(self.io, &rbuf);
        while (reader.interface.takeDelimiterInclusive('\n')) |line| {
            if (line.len <= 2) break; // the blank line ending the request head
        } else |_| {}

        if (self.mode == .stall) {
            while (!self.stopping.load(.acquire)) self.io.sleep(.fromMilliseconds(5), .awake) catch break;
            return;
        }
        var wbuf: [64 * 1024]u8 = undefined;
        var writer = stream.writer(self.io, &wbuf);
        writer.interface.writeAll(self.head) catch return;
        if (self.mode == .flood) {
            const chunk = [_]u8{'x'} ** 4096;
            while (true) writer.interface.writeAll(&chunk) catch break;
        }
        writer.interface.flush() catch {};
    }
};

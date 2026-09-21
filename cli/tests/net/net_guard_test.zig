// The guard every outbound request inherits, driven against a real loopback server through
// serverClient.rawRequest — the server-connect path, which skips only the host check
// (net.RequestOptions.allow_named_host) and must still cap the body and refuse a redirect.
const std = @import("std");
const net = @import("../../src/net.zig");
const server = @import("../../src/server/client.zig");
const testing = std.testing;

test "a response over the fetch cap is refused, not buffered" {
    var fake = try Fake.start("HTTP/1.1 200 OK\r\nContent-Length: 134217728\r\n\r\n", true);
    defer fake.stop();
    const url = try fake.url(testing.allocator);
    defer testing.allocator.free(url);

    try testing.expectError(error.HttpFailed, server.rawRequest(testing.allocator, fake.io, url, .GET, null, &.{}));
    try testing.expectEqual(@as(u32, 1), fake.hits.load(.acquire));
}

test "a redirect is refused, and the target is never fetched" {
    // The classic pivot: a server the user named 302s to the cloud-metadata address.
    var fake = try Fake.start("HTTP/1.1 302 Found\r\nLocation: http://169.254.169.254/latest/meta-data/\r\nContent-Length: 0\r\n\r\n", false);
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

/// A one-request HTTP server on loopback: it answers with `head`, then floods bytes when
/// `flood` is set (to run past the 64 MiB cap) — the write fails once the client gives up.
const Fake = struct {
    threaded: *std.Io.Threaded,
    listener: std.Io.net.Server,
    io: std.Io,
    port: u16,
    head: []const u8,
    flood: bool,
    hits: std.atomic.Value(u32) = .init(0),
    thread: std.Thread = undefined,

    fn start(head: []const u8, flood: bool) !*Fake {
        const a = testing.allocator;
        const threaded = try a.create(std.Io.Threaded);
        threaded.* = std.Io.Threaded.init(a, .{});
        const io = threaded.io();
        const self = try a.create(Fake);
        var port: u16 = 39871;
        self.* = while (port < 39931) : (port += 1) {
            var addr = std.Io.net.IpAddress.parse("127.0.0.1", port) catch unreachable;
            const l = addr.listen(io, .{ .reuse_address = true }) catch continue;
            break .{ .threaded = threaded, .listener = l, .io = io, .port = port, .head = head, .flood = flood };
        } else return error.NoFreePort;
        self.thread = try std.Thread.spawn(.{}, serve, .{self});
        return self;
    }

    fn stop(self: *Fake) void {
        const a = testing.allocator;
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

        var wbuf: [64 * 1024]u8 = undefined;
        var writer = stream.writer(self.io, &wbuf);
        writer.interface.writeAll(self.head) catch return;
        if (self.flood) {
            const chunk = [_]u8{'x'} ** 4096;
            while (true) writer.interface.writeAll(&chunk) catch break;
        }
        writer.interface.flush() catch {};
    }
};

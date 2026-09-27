//! The server-target guard and its pinned dial, over a fake resolver: an Io whose lookups answer
//! the test's names and whose dials are recorded, and pass to the real Io only for loopback, so no
//! packet leaves. A server name is judged by every address it resolves to, once per connection.
const std = @import("std");
const net = @import("../../src/net.zig");
const server = @import("../../src/server/client.zig");
const testing = std.testing;
const IpAddress = std.Io.net.IpAddress;

var base_io: std.Io = undefined;
var name_lookups: std.atomic.Value(usize) = .init(0);
var flip_lookups: std.atomic.Value(usize) = .init(0);
var dialled: [16]IpAddress = undefined;
var dials: std.atomic.Value(usize) = .init(0);

fn answers(name: []const u8) ?[]const []const u8 {
    if (std.mem.eql(u8, name, "meta.test")) return &.{"169.254.169.254"};
    if (std.mem.eql(u8, name, "mixed.test")) return &.{ "93.184.216.34", "fd00:ec2::254" };
    if (std.mem.eql(u8, name, "lan.test")) return &.{"192.168.7.7"};
    if (std.mem.eql(u8, name, "loop.test")) return &.{"127.0.0.1"};
    // Rebinding: a loopback answer first, the metadata address on every later lookup.
    if (std.mem.eql(u8, name, "flip.test")) return if (flip_lookups.fetchAdd(1, .acq_rel) == 0) &.{"127.0.0.1"} else &.{"169.254.169.254"};
    return null;
}

fn fakeLookup(ud: ?*anyopaque, hn: std.Io.net.HostName, q: *std.Io.Queue(std.Io.net.HostName.LookupResult), o: std.Io.net.HostName.LookupOptions) std.Io.net.HostName.LookupError!void {
    const list = answers(hn.bytes) orelse return base_io.vtable.netLookup(ud, hn, q, o);
    defer q.close(base_io);
    _ = name_lookups.fetchAdd(1, .acq_rel);
    for (list) |text| q.putOne(base_io, .{ .address = IpAddress.parse(text, o.port) catch unreachable }) catch {};
}

fn fakeDial(ud: ?*anyopaque, ip: *const IpAddress, o: IpAddress.ConnectOptions) IpAddress.ConnectError!std.Io.net.Socket {
    const i = dials.fetchAdd(1, .acq_rel);
    if (i < dialled.len) dialled[i] = ip.*;
    const loop = IpAddress.parse("127.0.0.1", ip.getPort()) catch unreachable;
    return if (ip.eql(&loop)) base_io.vtable.netConnectIp(ud, ip, o) else error.ConnectionRefused;
}

/// The fake Io over a fresh real one; counters reset. `vt` must outlive every call made through it.
const Rig = struct {
    threaded: std.Io.Threaded,
    vt: std.Io.VTable = undefined,

    fn init(self: *Rig) void {
        self.* = .{ .threaded = std.Io.Threaded.init(testing.allocator, .{}) };
        base_io = self.threaded.io();
        self.vt = base_io.vtable.*;
        self.vt.netLookup = fakeLookup;
        self.vt.netConnectIp = fakeDial;
        name_lookups.store(0, .release);
        flip_lookups.store(0, .release);
        dials.store(0, .release);
    }

    fn io(self: *Rig) std.Io {
        return .{ .userdata = base_io.userdata, .vtable = &self.vt };
    }

    fn metadataDialled() bool {
        const meta = IpAddress.parse("169.254.169.254", 0) catch unreachable;
        for (dialled[0..@min(dials.load(.acquire), dialled.len)]) |ip| {
            var at = ip;
            at.setPort(0);
            if (at.eql(&meta)) return true;
        }
        return false;
    }
};

fn connectVia(url: []const u8) !void {
    var rig: Rig = undefined;
    rig.init();
    defer rig.threaded.deinit();
    var client = server.connect(testing.allocator, rig.io(), url, "tok") catch return;
    client.deinit();
    return error.TestUnexpectedResult; // every non-loopback dial is refused, so no connect succeeds
}

test "a server name that resolves to a metadata address is refused before any dial" {
    try connectVia("http://meta.test:8090");
    try testing.expectEqual(@as(usize, 1), name_lookups.load(.acquire));
    try testing.expectEqual(@as(usize, 0), dials.load(.acquire));
    try connectVia("http://mixed.test:8090"); // one blocked address among good ones is enough
    try testing.expectEqual(@as(usize, 0), dials.load(.acquire));
}

test "a private server is dialled at the address judged: a literal as written, a name once resolved" {
    const lan = IpAddress.parse("192.168.7.7", 8090) catch unreachable;
    try connectVia("http://192.168.7.7:8090");
    try testing.expect(dials.load(.acquire) >= 1 and dialled[0].eql(&lan));
    try connectVia("http://lan.test:8090");
    try testing.expectEqual(@as(usize, 1), name_lookups.load(.acquire)); // the name is never looked up again
    try testing.expect(dials.load(.acquire) >= 1 and dialled[0].eql(&lan));
}

fn listen(port: *u16, from: u16) !std.Io.net.Server {
    port.* = from;
    while (port.* < from + 90) : (port.* += 1) {
        var addr = IpAddress.parse("127.0.0.1", port.*) catch unreachable;
        return addr.listen(base_io, .{ .reuse_address = true }) catch continue;
    }
    return error.NoFreePort;
}

fn answerOnce(listener: *std.Io.net.Server) void {
    const conn = listener.accept(base_io) catch return;
    defer conn.close(base_io);
    var rbuf: [1024]u8 = undefined;
    var r = conn.reader(base_io, &rbuf);
    while (r.interface.takeDelimiterInclusive('\n')) |line| {
        if (line.len <= 2) break; // the blank line ending the request head
    } else |_| {}
    var wbuf: [128]u8 = undefined;
    var w = conn.writer(base_io, &wbuf);
    w.interface.writeAll("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok") catch return;
    w.interface.flush() catch {};
}

/// One exchange with `host` at a loopback listener that answers 200.
fn exchangeWith(rig: *Rig, host: []const u8) !net.Response {
    var port: u16 = 0;
    var listener = try listen(&port, 40211);
    defer listener.deinit(base_io);
    const t = try std.Thread.spawn(.{}, answerOnce, .{&listener});
    defer t.join();
    var url_buf: [64]u8 = undefined;
    const url = try std.fmt.bufPrint(&url_buf, "http://{s}:{d}/x", .{ host, port });
    return net.request(testing.allocator, rig.io(), url, .{ .server_target = true }) catch |e| {
        const wake = IpAddress.parse("127.0.0.1", port) catch unreachable; // let the listener thread end
        if (wake.connect(base_io, .{ .mode = .stream })) |s| s.close(base_io) else |_| {}
        return e;
    };
}

test "a pinned exchange is answered over the connection it dialled, never a second lookup" {
    var rig: Rig = undefined;
    rig.init();
    defer rig.threaded.deinit();
    const res = try exchangeWith(&rig, "loop.test");
    defer testing.allocator.free(res.body);
    try testing.expectEqual(@as(u16, 200), res.status);
    try testing.expectEqualStrings("ok", res.body);
    try testing.expectEqual(@as(usize, 1), name_lookups.load(.acquire));
}

test "rebinding: an exchange whose next lookup turns to a metadata address never dials it" {
    var rig: Rig = undefined;
    rig.init();
    defer rig.threaded.deinit();
    const first = try exchangeWith(&rig, "flip.test");
    testing.allocator.free(first.body);
    try testing.expectError(error.BlockedHost, exchangeWith(&rig, "flip.test"));
    try testing.expectEqual(@as(usize, 2), flip_lookups.load(.acquire)); // one lookup per exchange
    try testing.expect(!Rig.metadataDialled());
}

test "rebinding: the events feed dials the address it judged, and refuses a metadata answer" {
    var rig: Rig = undefined;
    rig.init();
    defer rig.threaded.deinit();
    var port: u16 = 0;
    var listener = try listen(&port, 40311); // the edit port: the REST port + 1
    defer listener.deinit(base_io);
    var url_buf: [64]u8 = undefined;
    const base = try std.fmt.bufPrint(&url_buf, "http://flip.test:{d}", .{port - 1});
    var conn = try server.EditConn.open(testing.allocator, rig.io(), base, "tok", "c1");
    conn.deinit();
    try testing.expectError(error.BlockedHost, server.EditConn.open(testing.allocator, rig.io(), base, "tok", "c1"));
    try testing.expectEqual(@as(usize, 2), flip_lookups.load(.acquire)); // one lookup per open
    try testing.expect(!Rig.metadataDialled());
}

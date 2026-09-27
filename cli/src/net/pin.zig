//! The pinned dial for a server the user named: its host is resolved ONCE per connection, every
//! address judged by blockedRanges.json's `serverTarget`, and only those addresses are dialled — an
//! HTTP connection left in the client's pool under the URL's own host (TLS still verifies that
//! name), or the plain TCP stream of the events feed. Nothing looks the name up a second time.
const std = @import("std");
const ranges = @import("ranges.zig");

const Client = std.http.Client;
const IpAddress = std.Io.net.IpAddress;

/// Addresses the guard already judged for a host.
pub const Pins = []const IpAddress;

/// The most addresses one host is dialled at: the rest of a long answer is never raced.
pub const max_pins = 8;

/// What a server's host resolves to under `serverTarget` (private admitted): its addresses, or a
/// refusal when ANY of them is blocked. A literal resolves to itself, with no query sent.
pub const Resolved = union(enum) { ok: []IpAddress, blocked, unresolved: anyerror };

pub fn resolve(io: std.Io, host: []const u8, out: []IpAddress) Resolved {
    if (host.len == 0 or host.len > std.Io.net.HostName.max_len) return .{ .unresolved = error.UnknownHostName };
    const hn: std.Io.net.HostName = .{ .bytes = host }; // unvalidated, as std.http dials a URL's host
    var buf: [32]std.Io.net.HostName.LookupResult = undefined;
    var q: std.Io.Queue(std.Io.net.HostName.LookupResult) = .init(&buf);
    hn.lookup(io, &q, .{ .port = 0 }) catch |e| return .{ .unresolved = e };
    var n: usize = 0;
    while (q.getOne(io)) |res| switch (res) {
        .address => |ip| {
            if (ranges.blocked(ranges.Addr.fromIp(ip), .server_target, .{ .allow_private = true })) return .blocked;
            if (n < out.len) out[n] = ip;
            n += 1;
        },
        .canonical_name => {},
    } else |_| {}
    return if (n == 0) .{ .unresolved = error.UnknownHostName } else .{ .ok = out[0..@min(n, out.len)] };
}

/// Race `dialOne(ctx, pin)` over every pin, as std's own connect races a name's addresses: the
/// first to connect wins and `drop(ctx, loser)` closes the rest. The error is the last dial's.
fn race(comptime T: type, io: std.Io, pins: Pins, ctx: anytype, comptime dialOne: fn (@TypeOf(ctx), IpAddress) anyerror!T, comptime drop: fn (@TypeOf(ctx), T) void) !T {
    const Q = std.Io.Queue(anyerror!T);
    const Worker = struct {
        fn run(c: @TypeOf(ctx), ip: IpAddress, q: *Q, wio: std.Io) std.Io.Cancelable!void {
            const got = dialOne(c, ip);
            if (got) |_| {} else |e| if (e == error.Canceled) return error.Canceled;
            q.putOne(wio, got) catch |e| {
                if (got) |v| drop(c, v) else |_| {}
                if (e == error.Canceled) return error.Canceled;
            };
        }
    };
    const n = @min(pins.len, max_pins);
    var buf: [max_pins]anyerror!T = undefined;
    var q: Q = .init(&buf);
    var group: std.Io.Group = .init;
    for (pins[0..n]) |ip| group.async(io, Worker.run, .{ ctx, ip, &q, io });
    var last: anyerror = error.UnknownHostName;
    var won: ?T = null;
    for (0..n) |_| {
        const got = q.getOne(io) catch |e| {
            last = e;
            break;
        };
        won = got catch |e| {
            last = e;
            continue;
        };
        break;
    }
    group.cancel(io);
    q.close(io);
    while (q.getOneUncancelable(io)) |got| {
        const loser = got catch continue; // the winner already left the queue
        drop(ctx, loser);
    } else |_| {}
    return won orelse return last;
}

const HttpDial = struct { client: *Client, port: u16, protocol: Client.Protocol, host: std.Io.net.HostName };

fn httpOne(d: *const HttpDial, ip: IpAddress) anyerror!*Client.Connection {
    var lbuf: [64]u8 = undefined;
    return d.client.connectTcpOptions(.{
        .host = .{ .bytes = literal(ip, &lbuf) },
        .port = d.port,
        .protocol = d.protocol,
        .proxied_host = d.host,
        .proxied_port = d.port,
    });
}

fn httpDrop(d: *const HttpDial, c: *Client.Connection) void {
    c.closing = true;
    d.client.connection_pool.release(c, d.client.io);
}

/// Open `url`'s connection at one of `pins` and leave it free in `client`'s pool, where the
/// exchange's own connect finds it under the URL's host instead of resolving it again.
pub fn dial(client: *Client, url: []const u8, pins: Pins) !void {
    const io = client.io;
    const uri = try std.Uri.parse(url);
    const protocol = Client.Protocol.fromUri(uri) orelse return error.UnsupportedUriScheme;
    var hbuf: [std.Io.net.HostName.max_len]u8 = undefined;
    const d = HttpDial{ .client = client, .port = uri.port orelse if (protocol == .tls) 443 else 80, .protocol = protocol, .host = try uri.getHost(&hbuf) };
    if (protocol == .tls and client.now == null) { // the CA bundle client.request loads before its own dial
        const now = std.Io.Clock.real.now(io);
        try client.ca_bundle.rescan(client.allocator, io, now);
        client.now = now;
    }
    const conn = try race(*Client.Connection, io, pins, &d, httpOne, httpDrop);
    client.connection_pool.release(conn, io);
}

const TcpDial = struct { io: std.Io, port: u16 };

fn tcpOne(d: *const TcpDial, ip: IpAddress) anyerror!std.Io.net.Stream {
    var at = ip;
    at.setPort(d.port);
    return at.connect(d.io, .{ .mode = .stream });
}

fn tcpDrop(d: *const TcpDial, s: std.Io.net.Stream) void {
    s.close(d.io);
}

/// A plain TCP stream to `port` at one of `pins`.
pub fn connect(io: std.Io, pins: Pins, port: u16) !std.Io.net.Stream {
    const d = TcpDial{ .io = io, .port = port };
    return race(std.Io.net.Stream, io, pins, &d, tcpOne, tcpDrop);
}

/// The address as a resolver reads a literal: dotted quad, or IPv6 text without brackets or port.
fn literal(ip: IpAddress, buf: *[64]u8) []const u8 {
    var w: std.Io.Writer = .fixed(buf);
    switch (ip) {
        .ip4 => |a| w.print("{d}.{d}.{d}.{d}", .{ a.bytes[0], a.bytes[1], a.bytes[2], a.bytes[3] }) catch unreachable,
        .ip6 => |a| w.print("{f}", .{std.Io.net.Ip6Address.Unresolved{ .bytes = a.bytes, .interface_name = null }}) catch unreachable,
    }
    return w.buffered();
}

const testing = std.testing;

test "literal: the dotted quad and the bare IPv6 text a resolver reads back as itself" {
    var buf: [64]u8 = undefined;
    const v4 = try IpAddress.parse("192.168.1.20", 80);
    try testing.expectEqualStrings("192.168.1.20", literal(v4, &buf));
    const v6 = try IpAddress.parse("fd00::1", 80);
    try testing.expectEqualStrings("fd00::1", literal(v6, &buf));
}

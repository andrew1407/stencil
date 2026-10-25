//! `/connect` against a gated server on loopback: with no token typed, the URL's
//! `STENCIL_SERVER_TOKENS` entry (else `STENCIL_SERVER_TOKEN`) is what it dials with, a typed
//! token still wins, and the token itself never reaches the console's output.
const std = @import("std");
const console = @import("../../src/console.zig");
const args = @import("../../src/args.zig");
const logo = @import("../../src/app/logo.zig");
const Capture = @import("console_harness.zig").Capture;
const testing = std.testing;

/// A server that knows one session token, `good`, and mints for nobody: a connection
/// stands only if the handshake carried that token.
const Gate = struct {
    const good = "env-secret-tok";

    threaded: std.Io.Threaded,
    listener: std.Io.net.Server,
    port: u16,
    thread: std.Thread = undefined,
    stopping: std.atomic.Value(bool) = .init(false),

    fn start(self: *Gate) !void {
        self.threaded = std.Io.Threaded.init(testing.allocator, .{});
        const io = self.threaded.io();
        self.port = 40121;
        self.listener = while (self.port < 40200) : (self.port += 1) {
            var addr = std.Io.net.IpAddress.parse("127.0.0.1", self.port) catch unreachable;
            break addr.listen(io, .{ .reuse_address = true }) catch continue;
        } else return error.NoFreePort;
        self.thread = try std.Thread.spawn(.{}, serve, .{self});
    }

    fn stop(self: *Gate) void {
        const io = self.threaded.io();
        self.stopping.store(true, .release);
        // One loopback dial wakes the accept on every OS: Windows refuses shutdown on a listener.
        const wake = std.Io.net.IpAddress.parse("127.0.0.1", self.port) catch unreachable;
        if (wake.connect(io, .{ .mode = .stream })) |s| s.close(io) else |_| {}
        self.thread.join();
        self.listener.deinit(io);
        self.threaded.deinit();
    }

    fn serve(self: *Gate) void {
        const io = self.threaded.io();
        while (true) {
            const stream = self.listener.accept(io) catch return;
            defer stream.close(io);
            if (self.stopping.load(.acquire)) return;
            var rbuf: [4096]u8 = undefined;
            var reader = stream.reader(io, &rbuf);
            var probe = false;
            var authorized = false;
            while (reader.interface.takeDelimiterInclusive('\n')) |line| {
                if (line.len <= 2) break; // the blank line ending the request head
                if (std.mem.startsWith(u8, line, "GET /auth/session ")) probe = true;
                if (std.ascii.startsWithIgnoreCase(line, "authorization: Bearer " ++ good ++ "\r")) authorized = true;
            } else |_| {}
            const ok = probe and authorized;
            const body = if (ok) "{\"sessionId\":\"s1\",\"expiresAt\":1}" else "{\"code\":\"unauthorized\",\"message\":\"no\"}";
            var wbuf: [512]u8 = undefined;
            var writer = stream.writer(io, &wbuf);
            writer.interface.print("HTTP/1.1 {s}\r\nContent-Type: application/json\r\nContent-Length: {d}\r\nConnection: close\r\n\r\n{s}", .{
                if (ok) "200 OK" else "401 Unauthorized", body.len, body,
            }) catch continue;
            writer.interface.flush() catch {};
        }
    }
};

/// Whether `/connect <url><typed>` stands with the environment's `tokens`.
fn connects(io: std.Io, url: []const u8, tokens: args.EnvTokens, typed: []const u8) !bool {
    var session = console.Session{ .gpa = testing.allocator, .server_tokens = tokens };
    defer session.deinit();
    var line_buf: [128]u8 = undefined;
    _ = try console.handle(&session, io, try std.fmt.bufPrint(&line_buf, "/connect {s}{s}", .{ url, typed }));
    return session.servers.items.len == 1;
}

test "/connect with no token dials with the URL's STENCIL_SERVER_TOKENS entry, else STENCIL_SERVER_TOKEN" {
    var gate: Gate = undefined;
    try gate.start();
    defer gate.stop();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var out = Capture.init(testing.allocator);
    defer out.deinit();
    out.install();
    defer logo.clearSink();

    var url_buf: [64]u8 = undefined;
    const url = try std.fmt.bufPrint(&url_buf, "http://127.0.0.1:{d}", .{gate.port});
    var map_buf: [160]u8 = undefined;
    const map = try std.fmt.bufPrint(&map_buf, "http://127.0.0.1:1=wrong-tok, {s}={s}", .{ url, Gate.good });

    try testing.expect(!try connects(io, url, .{}, "")); // nothing to dial with: an anonymous mint, refused
    try testing.expect(try connects(io, url, .{ .single = Gate.good }, ""));
    try testing.expect(try connects(io, url, .{ .per_origin = map, .single = "wrong-tok" }, "")); // its origin's entry
    try testing.expect(!try connects(io, url, .{ .per_origin = map, .single = Gate.good }, " typed-wrong-tok")); // typed wins
    try testing.expect(std.mem.indexOf(u8, out.text(), "connected to") != null);
    try testing.expect(std.mem.indexOf(u8, out.text(), Gate.good) == null); // never printed
}

/// Whether `/open <app>#connect=<url>` stands with the environment's `tokens`.
fn linkConnects(io: std.Io, url: []const u8, tokens: args.EnvTokens) !bool {
    var session = console.Session{ .gpa = testing.allocator, .server_tokens = tokens };
    defer session.deinit();
    var line_buf: [160]u8 = undefined;
    _ = try console.handle(&session, io, try std.fmt.bufPrint(&line_buf, "/open https://app.example/#connect={s}", .{url}));
    return session.servers.items.len == 1;
}

test "a launch link's server never gets STENCIL_SERVER_TOKEN; its own STENCIL_SERVER_TOKENS entry still applies" {
    var gate: Gate = undefined;
    try gate.start();
    defer gate.stop();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var out = Capture.init(testing.allocator);
    defer out.deinit();
    out.install();
    defer logo.clearSink();

    var url_buf: [64]u8 = undefined;
    const url = try std.fmt.bufPrint(&url_buf, "http://127.0.0.1:{d}", .{gate.port});
    var map_buf: [160]u8 = undefined;
    const map = try std.fmt.bufPrint(&map_buf, "{s}={s}", .{ url, Gate.good });

    try testing.expect(!try linkConnects(io, url, .{ .single = Gate.good })); // the unscoped token stays home
    try testing.expect(try linkConnects(io, url, .{ .per_origin = map }));
    try testing.expect(std.mem.indexOf(u8, out.text(), Gate.good) == null);
}

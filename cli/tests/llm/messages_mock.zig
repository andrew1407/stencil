//! A loopback Anthropic Messages endpoint for the direct-wire suites: it answers every POST
//! with one canned status and body until stopped, and records what the last request carried —
//! its request line, headers and body — so a test sees exactly what left the CLI. Plain http
//! on 127.0.0.1: the one place the transport lets a key travel without TLS.
const std = @import("std");
const testing = std.testing;

pub const Mock = struct {
    threaded: *std.Io.Threaded,
    listener: std.Io.net.Server,
    io: std.Io,
    port: u16,
    status: []const u8,
    reply: []const u8,
    thread: std.Thread = undefined,
    hits: usize = 0,
    head: std.ArrayList(u8) = .empty, // the request line and headers, as sent
    body: std.ArrayList(u8) = .empty,

    pub fn start(status: []const u8, reply: []const u8) !*Mock {
        const a = testing.allocator;
        const threaded = try a.create(std.Io.Threaded);
        threaded.* = std.Io.Threaded.init(a, .{});
        const io = threaded.io();
        const self = try a.create(Mock);
        var port: u16 = 40211;
        self.* = while (port < 40271) : (port += 1) {
            var addr = std.Io.net.IpAddress.parse("127.0.0.1", port) catch unreachable;
            const l = addr.listen(io, .{ .reuse_address = true }) catch continue;
            break .{ .threaded = threaded, .listener = l, .io = io, .port = port, .status = status, .reply = reply };
        } else return error.NoFreePort;
        self.thread = try std.Thread.spawn(.{}, serve, .{self});
        return self;
    }

    /// Stop serving; what was recorded stays readable until `deinit`.
    pub fn stop(self: *Mock) void {
        // Only shutdown wakes a thread blocked in accept on Linux; close alone leaves it asleep.
        (std.Io.net.Stream{ .socket = self.listener.socket }).shutdown(self.io, .both) catch {};
        self.listener.deinit(self.io);
        self.thread.join();
    }

    pub fn deinit(self: *Mock) void {
        const a = testing.allocator;
        self.head.deinit(a);
        self.body.deinit(a);
        self.threaded.deinit();
        a.destroy(self.threaded);
        a.destroy(self);
    }

    /// `http://127.0.0.1:<port>`, caller-owned.
    pub fn base(self: *const Mock, a: std.mem.Allocator) ![]u8 {
        return std.fmt.allocPrint(a, "http://127.0.0.1:{d}", .{self.port});
    }

    /// The value of header `name` in the recorded request (case-insensitive), or null.
    pub fn header(self: *const Mock, name: []const u8) ?[]const u8 {
        var it = std.mem.splitSequence(u8, self.head.items, "\r\n");
        while (it.next()) |line| {
            const colon = std.mem.indexOfScalar(u8, line, ':') orelse continue;
            if (std.ascii.eqlIgnoreCase(line[0..colon], name)) return std.mem.trim(u8, line[colon + 1 ..], " ");
        }
        return null;
    }

    fn serve(self: *Mock) void {
        while (true) {
            const stream = self.listener.accept(self.io) catch return;
            self.answer(stream) catch {};
            stream.close(self.io);
        }
    }

    fn answer(self: *Mock, stream: std.Io.net.Stream) !void {
        const a = testing.allocator;
        var rbuf: [8192]u8 = undefined;
        var reader = stream.reader(self.io, &rbuf);
        self.head.clearRetainingCapacity();
        self.body.clearRetainingCapacity();
        var length: usize = 0;
        while (true) {
            const line = try reader.interface.takeDelimiterInclusive('\n');
            if (line.len <= 2) break; // the blank line ending the head
            try self.head.appendSlice(a, line);
            const colon = std.mem.indexOfScalar(u8, line, ':') orelse continue;
            if (std.ascii.eqlIgnoreCase(line[0..colon], "content-length"))
                length = try std.fmt.parseInt(usize, std.mem.trim(u8, line[colon + 1 ..], " \r\n"), 10);
        }
        try self.body.resize(a, length);
        try reader.interface.readSliceAll(self.body.items);
        self.hits += 1;
        var wbuf: [4096]u8 = undefined;
        var writer = stream.writer(self.io, &wbuf);
        try writer.interface.print("HTTP/1.1 {s}\r\nContent-Type: application/json\r\nContent-Length: {d}\r\nConnection: close\r\n\r\n", .{ self.status, self.reply.len });
        try writer.interface.writeAll(self.reply);
        try writer.interface.flush();
    }
};

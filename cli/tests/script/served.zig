//! A loopback HTTP server for the script suites: it answers every GET for the files it was
//! given until stopped, so a `--script` run, a planner and an executor can each fetch the same
//! URL. Loopback passes the guard only for a URL the user named, which is what a script is.
const std = @import("std");
const testing = std.testing;

pub const File = struct { path: []const u8, body: []const u8 };

pub const Served = struct {
    threaded: *std.Io.Threaded,
    listener: std.Io.net.Server,
    io: std.Io,
    port: u16,
    files: []const File,
    thread: std.Thread = undefined,

    pub fn start(files: []const File) !*Served {
        const a = testing.allocator;
        const threaded = try a.create(std.Io.Threaded);
        threaded.* = std.Io.Threaded.init(a, .{});
        const io = threaded.io();
        const self = try a.create(Served);
        var port: u16 = 39941;
        self.* = while (port < 40001) : (port += 1) {
            var addr = std.Io.net.IpAddress.parse("127.0.0.1", port) catch unreachable;
            const l = addr.listen(io, .{ .reuse_address = true }) catch continue;
            break .{ .threaded = threaded, .listener = l, .io = io, .port = port, .files = files };
        } else return error.NoFreePort;
        self.thread = try std.Thread.spawn(.{}, serve, .{self});
        return self;
    }

    pub fn stop(self: *Served) void {
        const a = testing.allocator;
        self.listener.deinit(self.io); // unblocks the accept, ending the thread
        self.thread.join();
        self.threaded.deinit();
        a.destroy(self.threaded);
        a.destroy(self);
    }

    /// `http://127.0.0.1:<port>/<path>`, caller-owned.
    pub fn url(self: *const Served, a: std.mem.Allocator, path: []const u8) ![]u8 {
        return std.fmt.allocPrint(a, "http://127.0.0.1:{d}/{s}", .{ self.port, path });
    }

    fn serve(self: *Served) void {
        while (true) {
            const stream = self.listener.accept(self.io) catch return;
            self.answer(stream);
            stream.close(self.io);
        }
    }

    fn answer(self: *Served, stream: std.Io.net.Stream) void {
        var rbuf: [4096]u8 = undefined;
        var reader = stream.reader(self.io, &rbuf);
        const first = reader.interface.takeDelimiterInclusive('\n') catch return;
        var words = std.mem.tokenizeScalar(u8, first, ' ');
        _ = words.next(); // the method
        const target = std.mem.trimStart(u8, words.next() orelse "", "/");
        var pbuf: [256]u8 = undefined; // the reads below reuse `rbuf`, which `target` points into
        const n = @min(target.len, pbuf.len);
        @memcpy(pbuf[0..n], target[0..n]);
        const path = pbuf[0..n];
        while (reader.interface.takeDelimiterInclusive('\n')) |line| {
            if (line.len <= 2) break; // the blank line ending the request head
        } else |_| {}

        var body: ?[]const u8 = null;
        for (self.files) |f| if (std.mem.eql(u8, f.path, path)) {
            body = f.body;
        };
        var wbuf: [64 * 1024]u8 = undefined;
        var writer = stream.writer(self.io, &wbuf);
        const status = if (body != null) "200 OK" else "404 Not Found";
        const b = body orelse "";
        writer.interface.print("HTTP/1.1 {s}\r\nContent-Length: {d}\r\nConnection: close\r\n\r\n", .{ status, b.len }) catch return;
        writer.interface.writeAll(b) catch return;
        writer.interface.flush() catch {};
    }
};

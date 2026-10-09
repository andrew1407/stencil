//! The two pipes a line-editor test drives `readLine` over: `in` carries the keys, `out` takes
//! what it paints. No tty — `readLine` only reads bytes.
const std = @import("std");

pub const Pipes = struct {
    in: [2]std.posix.fd_t,
    out: [2]std.posix.fd_t,

    pub fn open() !Pipes {
        const in = try std.Io.Threaded.pipe2(.{});
        errdefer for (in) |fd| {
            _ = std.c.close(fd);
        };
        return .{ .in = in, .out = try std.Io.Threaded.pipe2(.{}) };
    }

    /// Every end but `in[1]`, which a test closes itself to end the input.
    pub fn close(self: Pipes) void {
        _ = std.c.close(self.out[1]);
        _ = std.c.close(self.out[0]);
        _ = std.c.close(self.in[0]);
    }
};

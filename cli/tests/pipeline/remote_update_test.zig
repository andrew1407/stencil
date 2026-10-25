//! `--remote-update` delivering into the caller's server client: a session re-minted during the
//! upload lands in that client, so the caller's teardown frees the fresh token, once.
const std = @import("std");
const pipeline = @import("../../src/pipeline.zig");
const args = @import("../../src/args.zig");
const image = @import("../../src/media/image.zig");
const report = @import("../../src/app/report.zig");
const errors = @import("../../src/server/errors.zig");
const Client = @import("../../src/server/rest.zig").Client;
const testing = std.testing;

// The stored session died with a server restart: the first upload is refused, the credential
// mints a fresh one, and the retry carries it.
const Restarted = struct {
    var minted = false;

    fn run(gpa: std.mem.Allocator, _: std.Io, url: []const u8, _: std.http.Method, _: ?[]const u8, headers: []const std.http.Header) errors.TransportError![]u8 {
        if (std.mem.endsWith(u8, url, "/auth/token")) {
            minted = true;
            return gpa.dupe(u8, "{\"token\":\"fresh\"}");
        }
        for (headers) |h| if (std.ascii.eqlIgnoreCase(h.name, "authorization") and std.mem.eql(u8, h.value, "Bearer stale"))
            return errors.Error.Unauthorized;
        return gpa.dupe(u8, "{}");
    }

    fn mute(_: *anyopaque, _: report.Severity, _: []const u8) void {}
};

test "remote-update: a re-mint during the upload is kept by the caller's client" {
    const a = testing.allocator;
    var unused: u8 = 0;
    report.install(.{ .ctx = @ptrCast(&unused), .emitFn = Restarted.mute });
    defer report.uninstall();
    Restarted.minted = false;
    var client: Client = .{
        .gpa = a,
        .io = undefined, // the scripted transport never touches it
        .base = try a.dupe(u8, "http://s"),
        .token = try a.dupe(u8, "stale"),
        .auth = try a.dupe(u8, "Bearer stale"),
        .credential = try a.dupe(u8, "ADMIN"),
        .transport = Restarted.run,
    };
    defer client.deinit();
    var px = [_]u8{ 255, 0, 0, 255 };
    const img: image.Rgba8 = .{ .width = 1, .height = 1, .pixels = &px };
    var id = "p_1".*;
    const opts = args.Options{ .remote_update = true };
    try pipeline.deliverToServer(a, undefined, opts, img, .png, &client, &id, null, 1, 1);
    try testing.expect(Restarted.minted);
    try testing.expectEqualStrings("fresh", client.token);
    try testing.expectEqualStrings("Bearer fresh", client.auth);
}

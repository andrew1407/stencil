//! The full-screen echo of a sent line: a command word wears the accent as it does in /help —
//! the verbs and the transform shorthands alike — its arguments stay plain, and other text is
//! echoed as typed.
const std = @import("std");
const logo = @import("../../src/app/logo.zig");
const loop = @import("../../src/console/loop.zig");
const fx = @import("fx_harness.zig");
const testing = std.testing;

fn echoed(rig: *fx.Rig, line: []const u8) []const u8 {
    loop.echoCommand(&rig.session, &rig.scr, line);
    return rig.scr.lines.back().?;
}

test "echo: every command word is accented, its arguments are not" {
    var rig: fx.Rig = undefined;
    try rig.start(testing.allocator, 24, 100);
    defer rig.deinit();
    const accent = logo.accentSeq();
    for ([_][]const u8{ "/crop", "/rotate", "/filter", "/undo", "/save", "/status", "bw" }) |word| {
        var buf: [64]u8 = undefined;
        const line = try std.fmt.bufPrint(&buf, "{s} 1", .{word});
        const row = echoed(&rig, line);
        var want: [96]u8 = undefined;
        const head = try std.fmt.bufPrint(&want, "{s}{s}{s} 1", .{ accent, word, logo.resetSeq() });
        try testing.expect(std.mem.indexOf(u8, row, head) != null);
    }
    const plain = echoed(&rig, "hello there");
    try testing.expect(std.mem.indexOf(u8, plain, "hello there") != null);
}

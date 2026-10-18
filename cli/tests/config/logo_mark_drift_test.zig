//! Drift guard for the console logo: core/format/logoBanner.cpp is a port of the raster in
//! src/app/logo/mark.zig, and the browser and desktop CLI modes paint the core's rows, so every
//! row of every mark at both sizes is pinned here to what the CLI prints.
const std = @import("std");
const mark = @import("../../src/app/logo/mark.zig");
const skin = @import("../../src/app/skin.zig");
const c = @import("../../src/core/cApi.zig");
const testing = std.testing;

fn coreRows(a: std.mem.Allocator, which: [*:0]const u8, compact: bool) ![]u8 {
    const needed = c.stencil_cli_logoBanner(which, @intFromBool(compact), 0, null, 0);
    try testing.expect(needed > 0);
    const buf = try a.alloc(u8, @intCast(needed));
    try testing.expectEqual(needed, c.stencil_cli_logoBanner(which, @intFromBool(compact), 0, buf.ptr, needed));
    return buf[0 .. buf.len - 1];
}

fn checkMark(a: std.mem.Allocator, sk: skin.Skin, which: [*:0]const u8, comptime compact: bool) !void {
    const rows = try coreRows(a, which, compact);
    defer a.free(@as([*]u8, @ptrCast(rows.ptr))[0 .. rows.len + 1]);
    var it = std.mem.splitScalar(u8, rows, '\n');
    var row: usize = 0;
    var buf: [128]u8 = undefined;
    while (it.next()) |want| : (row += 1) {
        try testing.expectEqualStrings(want, mark.rowText(compact, sk, row, &buf));
    }
    try testing.expectEqual(@as(usize, if (compact) 5 else 6), row);
}

test "logo mark: the core's banner rows are the CLI's, for the S, the P and the M" {
    const a = testing.allocator;
    inline for (.{ false, true }) |compact| {
        try checkMark(a, .none, "S", compact);
        try checkMark(a, .pie, "P", compact);
        try checkMark(a, .bifrost, "M", compact);
    }
    try testing.expectEqual(@as(c_int, 0), c.stencil_cli_logoBanner("Q", 0, 0, null, 0));
}

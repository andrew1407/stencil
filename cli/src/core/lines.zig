//! The line-list half of the core bridge: the layout caps, and the co-edit merge's keep mask with
//! the lines crossing as core/abi/linesCodec.hpp's flat pair (nums + text). The encoder is the
//! host side of that byte layout, as browser/js/core/line/linesCodec.js encodeLines is for wasm.
const std = @import("std");
const LineDraw = @import("../core.zig").LineDraw;
const testing = std.testing;

const c = @cImport({
    @cInclude("core/cliApi.h");
});

/// What a layout may hold (constants.json LIMITS): lines kept, points per line, points in all.
pub const LayoutCaps = struct { lines: usize, line_points: usize, points: usize };

pub fn layoutCaps() LayoutCaps {
    var lines: c_int = 0;
    var line_points: c_int = 0;
    var points: c_int = 0;
    c.stencil_cli_layoutCaps(&lines, &line_points, &points);
    return .{ .lines = @intCast(lines), .line_points = @intCast(line_points), .points = @intCast(points) };
}

const Encoded = struct { nums: []f64, text: []u8 };

fn encode(a: std.mem.Allocator, lines: []const LineDraw) !Encoded {
    var n: usize = 1;
    var t: usize = 0;
    for (lines) |l| {
        n += 8 + l.points.len;
        t += l.color.len + l.style.len + l.fill_color.len + l.point_color.len;
    }
    const out = Encoded{ .nums = try a.alloc(f64, n), .text = try a.alloc(u8, t) };
    out.nums[0] = @floatFromInt(lines.len);
    var i: usize = 1;
    var k: usize = 0;
    for (lines) |l| {
        const fields = [_][]const u8{ l.color, l.style, l.fill_color, l.point_color };
        out.nums[i] = @floatFromInt(l.points.len / 2);
        out.nums[i + 1] = l.thickness;
        out.nums[i + 2] = l.point_size;
        out.nums[i + 3] = if (l.locked) 1 else 0;
        for (fields, 0..) |f, j| out.nums[i + 4 + j] = @floatFromInt(f.len);
        i += 8;
        @memcpy(out.nums[i..][0..l.points.len], l.points);
        i += l.points.len;
        for (fields) |f| {
            @memcpy(out.text[k..][0..f.len], f);
            k += f.len;
        }
    }
    return out;
}

/// keep[i]: whether local line i joins the peer's lines (core::mergeKeep). A line past what
/// the codec's caps let core decode is never kept. Caller owns the result.
pub fn mergeKeep(gpa: std.mem.Allocator, server: []const LineDraw, local: []const LineDraw) ![]bool {
    var arena = std.heap.ArenaAllocator.init(gpa);
    defer arena.deinit();
    const s = try encode(arena.allocator(), server);
    const l = try encode(arena.allocator(), local);
    const mask = try arena.allocator().alloc(u8, local.len);
    @memset(mask, 0);
    _ = c.stencil_cli_mergeLinesKeep(s.nums.ptr, @intCast(s.nums.len), s.text.ptr, @intCast(s.text.len), l.nums.ptr, @intCast(l.nums.len), l.text.ptr, @intCast(l.text.len), mask.ptr, @intCast(mask.len));
    const keep = try gpa.alloc(bool, local.len);
    for (mask, keep) |m, *k| k.* = m == 1;
    return keep;
}

fn line(pts: []const f64, color: [:0]const u8) LineDraw {
    return .{ .points = pts, .color = color, .thickness = 2, .point_size = 4, .style = "solid", .locked = false, .fill_color = "transparent" };
}

test "mergeKeep: a local line joins unless the peer or an earlier local line keys the same" {
    const a = testing.allocator;
    const peer = [_]LineDraw{line(&.{ 1, 2 }, "#f00")};
    const local = [_]LineDraw{ line(&.{ 1, 2 }, "#f00"), line(&.{ 3, 4 }, "#f00"), line(&.{ 3, 4 }, "#f00"), line(&.{ 1, 2 }, "#0f0") };
    const keep = try mergeKeep(a, &peer, &local);
    defer a.free(keep);
    try testing.expectEqualSlices(bool, &.{ false, true, false, true }, keep);
    const none = try mergeKeep(a, &.{}, &.{});
    defer a.free(none);
    try testing.expectEqual(@as(usize, 0), none.len);
}

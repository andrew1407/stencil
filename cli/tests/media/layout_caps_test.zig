//! The layout caps a parsed document is cut to, as browser/js/core/layout.js sanitizeLines cuts
//! it: core hands out the caps, which must equal constants.json LIMITS; the real per-line and
//! line caps through `parse`, and the total budget under small caps through `parseCapped`.
const std = @import("std");
const core = @import("../../src/core.zig");
const layout = @import("../../src/media/layout.zig");
const testing = std.testing;

const constants_json = @embedFile("constants.json");

fn limit(limits: std.json.ObjectMap, key: []const u8) usize {
    return @intCast(limits.get(key).?.integer);
}

test "drift: core's layout caps are constants.json LIMITS" {
    const parsed = try std.json.parseFromSlice(std.json.Value, testing.allocator, constants_json, .{});
    defer parsed.deinit();
    const limits = parsed.value.object.get("LIMITS").?.object;
    const caps = core.layoutCaps();
    try testing.expectEqual(limit(limits, "layoutLinesMax"), caps.lines);
    try testing.expectEqual(limit(limits, "layoutLinePointsMax"), caps.line_points);
    try testing.expectEqual(limit(limits, "layoutPointsMax"), caps.points);
}

/// `{"lines":[…]}` of `n_lines` lines holding `n_points` points each.
fn doc(a: std.mem.Allocator, n_lines: usize, n_points: usize) ![]u8 {
    var out: std.ArrayList(u8) = .empty;
    try out.appendSlice(a, "{\"lines\":[");
    for (0..n_lines) |l| {
        if (l > 0) try out.append(a, ',');
        try out.appendSlice(a, "{\"points\":[");
        for (0..n_points) |p| try out.print(a, "{s}{{\"x\":{d},\"y\":1}}", .{ if (p > 0) "," else "", p });
        try out.appendSlice(a, "]}");
    }
    try out.appendSlice(a, "]}");
    return out.toOwnedSlice(a);
}

test "parse cuts a line past the per-line cap and drops the lines past the line cap" {
    const a = testing.allocator;
    const caps = core.layoutCaps();
    const long = try doc(a, 1, caps.line_points + 10);
    defer a.free(long);
    var one = try layout.parse(a, long);
    defer one.deinit();
    try testing.expectEqual(caps.line_points * 2, one.lines[0].points.len);
    try testing.expectEqual(@as(f64, @floatFromInt(caps.line_points - 1)), one.lines[0].points[one.lines[0].points.len - 2]);

    const many = try doc(a, caps.lines + 10, 1);
    defer a.free(many);
    var capped = try layout.parse(a, many);
    defer capped.deinit();
    try testing.expectEqual(caps.lines, capped.lines.len);
}

test "the line that spends the last of the budget is cut there, and every line after it dropped" {
    const a = testing.allocator;
    const caps = core.LayoutCaps{ .lines = 4, .line_points = 3, .points = 7 };
    // Budget 7: five points cut to three (4 left), junk not counted, the pointless line counted but
    // not drawn, two points (2 left), three cut to two (0 left), and the last line dropped.
    const json =
        \\{"lines":[{"points":[{"x":0,"y":0},{"x":1,"y":0},{"x":2,"y":0},{"x":3,"y":0},{"x":4,"y":0}]},
        \\ "junk", {}, {"points":[{"x":5,"y":0},{"x":6,"y":0}]},
        \\ {"points":[{"x":7,"y":0},{"x":8,"y":0},{"x":9,"y":0}]}, {"points":[{"x":10,"y":0}]}]}
    ;
    var got = try layout.parseCapped(a, json, caps);
    defer got.deinit();
    try testing.expectEqual(@as(usize, 3), got.lines.len);
    try testing.expectEqualSlices(f64, &.{ 0, 0, 1, 0, 2, 0 }, got.lines[0].points);
    try testing.expectEqualSlices(f64, &.{ 5, 0, 6, 0 }, got.lines[1].points);
    try testing.expectEqualSlices(f64, &.{ 7, 0, 8, 0 }, got.lines[2].points);

    // The line cap counts line objects only: two of them fit, the junk between them is free.
    var two = try layout.parseCapped(a, "{\"lines\":[{\"points\":[{\"x\":1}]},7,{\"points\":[{\"x\":2}]},{\"points\":[{\"x\":3}]}]}", .{ .lines = 2, .line_points = 9, .points = 9 });
    defer two.deinit();
    try testing.expectEqual(@as(usize, 2), two.lines.len);
    try testing.expectEqual(@as(f64, 2), two.lines[1].points[0]);
}

// Parse the layout fixture, rasterise it onto a buffer, and apply a filter.
const std = @import("std");
const core = @import("../src/core.zig");
const layout_mod = @import("../src/layout.zig");
const testing = std.testing;
const layout_json = @embedFile("fixtures/layout.json");

test "layout parses + rasterises; bw filter greyscales" {
    const a = testing.allocator;
    var L = try layout_mod.parse(a, layout_json);
    defer L.deinit();
    try testing.expectEqual(@as(usize, 1), L.lines.len);
    try testing.expectEqualStrings("bw", L.filter.?);

    const w = 16;
    const h = 12;
    const buf = try a.alloc(u8, w * h * 4);
    defer a.free(buf);
    core.fillRGBA(buf, w * h, .{ .r = 255, .g = 255, .b = 255, .a = 255 });
    for (L.lines) |line| core.rasterizeLine(buf, w, h, line);

    const idx = (5 * w + 7) * 4; // near the red line's midpoint
    try testing.expect(buf[idx] > 120); // red raised
    try testing.expect(buf[idx + 2] < 180); // blue reduced from white

    var px = [_]u8{ 100, 150, 200, 255 };
    core.applyFilter(a, "bw", &px, 1, .{ .r = 0, .g = 0, .b = 0, .a = 255 });
    try testing.expectEqual(px[0], px[1]);
    try testing.expectEqual(px[1], px[2]);
    try testing.expectEqual(@as(u8, 255), px[3]);
}

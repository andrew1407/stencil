//! Layout JSON: a document parsed into drawable lines, and a point carried through a frame's
//! crop and rotate steps (`--layout-frame source`), then clamped and re-mapped in place.
const std = @import("std");
const core = @import("../../src/core.zig");
const layout = @import("../../src/media/layout.zig");
const FrameStep = layout.FrameStep;
const clampPoint = layout.clampPoint;
const mapPoint = layout.mapPoint;
const parse = layout.parse;
const remapLayoutDocAlloc = layout.remapLayoutDocAlloc;
const remapLinesArrayAlloc = layout.remapLinesArrayAlloc;
const testing = std.testing;

test "parse layout json into drawable lines" {
    const a = testing.allocator;
    const json =
        \\{ "imageWidth": 10, "imageHeight": 20, "filter": "bw",
        \\  "pageSize": "custom", "customPageWidth": 10, "customPageHeight": 15,
        \\  "lines": [ { "points": [{"x":1,"y":2},{"x":3,"y":4}],
        \\              "color": "red", "thickness": 3, "locked": true } ] }
    ;
    var L = try parse(a, json);
    defer L.deinit();
    try testing.expectEqual(@as(usize, 1), L.lines.len);
    try testing.expectEqual(@as(usize, 4), L.lines[0].points.len);
    try testing.expect(L.lines[0].locked);
    try testing.expectEqualStrings("bw", L.filter.?);
    try testing.expectEqualStrings("custom", L.page_size.?);
    try testing.expectEqual(@as(f64, 10), L.custom_page_w);
    try testing.expectEqual(@as(f64, 15), L.custom_page_h);
}

test "mapPoint: crop step subtracts the resolved origin (--layout-frame source)" {
    // Crop "x1=100px" resolves to origin (100, 0); source point (150,50) → (50,50).
    const steps = [_]FrameStep{.{ .crop = .{ .x = 100, .y = 0 } }};
    const p = mapPoint(&steps, .{ .x = 150, .y = 50 });
    try testing.expectEqual(@as(f64, 50), p.x);
    try testing.expectEqual(@as(f64, 50), p.y);
}

test "mapPoint: rotate step matches core rotateImageRGBA's pixel mapping" {
    const a = testing.allocator;
    // A 4x2 image with one red marker pixel; for each quarter count, rotating the image
    // through the core and mapping the marker's CENTER point must land in the same pixel.
    const mx: usize = 3;
    const my: usize = 0;
    inline for ([_]i32{ 1, 2, 3 }) |q| {
        var src = [_]u8{0} ** (4 * 2 * 4);
        src[(my * 4 + mx) * 4] = 255; // R of the marker
        const dims = core.rotatedDims(4, 2, q);
        const uw: usize = @intCast(dims.w);
        const uh: usize = @intCast(dims.h);
        const dst = try a.alloc(u8, uw * uh * 4);
        defer a.free(dst);
        core.rotateImageRGBA(&src, 4, 2, q, dst);

        const steps = [_]FrameStep{.{ .rotate = .{ .quarters = q, .w = 4, .h = 2 } }};
        const p = mapPoint(&steps, .{ .x = @as(f64, mx) + 0.5, .y = @as(f64, my) + 0.5 });
        const px: usize = @intFromFloat(@floor(p.x));
        const py: usize = @intFromFloat(@floor(p.y));
        try testing.expectEqual(@as(u8, 255), dst[(py * uw + px) * 4]);
    }
    // Explicit arithmetic: one CW turn of a 4x2 frame sends (3,0) → (h−y, x) = (2,3).
    const one = [_]FrameStep{.{ .rotate = .{ .quarters = 1, .w = 4, .h = 2 } }};
    const q1 = mapPoint(&one, .{ .x = 3, .y = 0 });
    try testing.expectEqual(@as(f64, 2), q1.x);
    try testing.expectEqual(@as(f64, 3), q1.y);
    // A negative count normalizes like core (−1 ≡ 3 CW quarters).
    const neg = [_]FrameStep{.{ .rotate = .{ .quarters = -1, .w = 4, .h = 2 } }};
    const three = [_]FrameStep{.{ .rotate = .{ .quarters = 3, .w = 4, .h = 2 } }};
    const pn = mapPoint(&neg, .{ .x = 3, .y = 0.5 });
    const p3 = mapPoint(&three, .{ .x = 3, .y = 0.5 });
    try testing.expectEqual(p3.x, pn.x);
    try testing.expectEqual(p3.y, pn.y);
}

test "mapPoint: crop then rotate compose in order; clampPoint bounds the result" {
    // Crop origin (100,0) leaves a 100x50 frame; then one CW quarter-turn.
    const steps = [_]FrameStep{
        .{ .crop = .{ .x = 100, .y = 0 } },
        .{ .rotate = .{ .quarters = 1, .w = 100, .h = 50 } },
    };
    // (150,10) → crop → (50,10) → rotate → (50−10, 50) = (40,50).
    const p = mapPoint(&steps, .{ .x = 150, .y = 10 });
    try testing.expectEqual(@as(f64, 40), p.x);
    try testing.expectEqual(@as(f64, 50), p.y);
    // Clamp into the rotated 50x100 bounds.
    const c = clampPoint(.{ .x = -3, .y = 250 }, 50, 100);
    try testing.expectEqual(@as(f64, 0), c.x);
    try testing.expectEqual(@as(f64, 100), c.y);
}

test "remapLinesArrayAlloc re-maps points, keeps other fields, clamps with no steps" {
    const a = testing.allocator;
    const steps = [_]FrameStep{.{ .crop = .{ .x = 100, .y = 0 } }};
    const out = try remapLinesArrayAlloc(a, "[{\"points\":[{\"x\":150,\"y\":50},{\"x\":90,\"y\":20}],\"color\":\"red\",\"thickness\":3}]", &steps, 100, 50);
    defer a.free(out);
    // (150,50) → (50,50); (90,20) → (−10,20) clamped to (0,20). Styling untouched.
    try testing.expect(std.mem.indexOf(u8, out, "{\"x\":50,\"y\":50}") != null);
    try testing.expect(std.mem.indexOf(u8, out, "{\"x\":0,\"y\":20}") != null);
    try testing.expect(std.mem.indexOf(u8, out, "\"color\":\"red\"") != null);
    try testing.expect(std.mem.indexOf(u8, out, "\"thickness\":3") != null);

    // No steps = clamp only.
    const clamped = try remapLinesArrayAlloc(a, "[{\"points\":[{\"x\":999,\"y\":-4}]}]", &.{}, 100, 50);
    defer a.free(clamped);
    try testing.expect(std.mem.indexOf(u8, clamped, "{\"x\":100,\"y\":0}") != null);

    // Malformed input comes back unchanged.
    const junk = try remapLinesArrayAlloc(a, "{\"not\":\"an array\"}", &.{}, 10, 10);
    defer a.free(junk);
    try testing.expectEqualStrings("{\"not\":\"an array\"}", junk);
}

test "remapLayoutDocAlloc re-maps the doc's lines and keeps the other fields" {
    const a = testing.allocator;
    const steps = [_]FrameStep{.{ .crop = .{ .x = 8, .y = 0 } }};
    const out = try remapLayoutDocAlloc(a, "{\"filter\":\"bw\",\"lines\":[{\"points\":[{\"x\":9,\"y\":2}]}]}", &steps, 8, 12);
    defer a.free(out);
    try testing.expect(std.mem.indexOf(u8, out, "{\"x\":1,\"y\":2}") != null);
    try testing.expect(std.mem.indexOf(u8, out, "\"filter\":\"bw\"") != null);
}

// Walks the shared layout conformance vectors (browser/js/config/fixtures/layout/) against the cli's
// real tolerant reader (layout.parse — the path /apply and --layout use): sparse.json to expectFilled,
// payload.json through the parse side. Measured cli drift is pinned via fixture_overrides.json
// (`cliLines`): an empty-points line is skipped, every point OBJECT kept with missing coordinates 0, a
// numeric-string field falls back, non-bool locked is false. Canonical "imageFilter" beats "filter".
const std = @import("std");
const layout = @import("../src/media/layout.zig");
const core = @import("../src/core.zig");
const fx = @import("fixture_corpus.zig");
const testing = std.testing;

const line_defaults = .{
    .color = "#FFFF00",
    .thickness = 2.0,
    .point_size = 4.0,
    .style = "solid",
    .locked = false,
    .fill_color = "transparent",
    .point_color = "",
};

fn fieldStr(line: std.json.Value, key: []const u8, default: []const u8) []const u8 {
    return fx.memberStr(line, key) orelse default;
}

fn fieldNum(line: std.json.Value, key: []const u8, default: f64) f64 {
    const v = fx.member(line, key) orelse return default;
    return switch (v) {
        .integer => |i| @floatFromInt(i),
        .float => |f| f,
        else => default,
    };
}

/// Compare one parsed LineDraw against an expected fixture line ({points,...} with
/// absent fields meaning the shared per-line defaults).
fn expectLine(got: core.LineDraw, want: std.json.Value) !void {
    const pts = fx.member(want, "points").?.array.items;
    try testing.expectEqual(pts.len * 2, got.points.len);
    for (pts, 0..) |p, i| {
        try testing.expectEqual(fieldNum(p, "x", 0), got.points[i * 2]);
        try testing.expectEqual(fieldNum(p, "y", 0), got.points[i * 2 + 1]);
    }
    try testing.expectEqualStrings(fieldStr(want, "color", line_defaults.color), got.color);
    try testing.expectEqual(fieldNum(want, "thickness", line_defaults.thickness), got.thickness);
    try testing.expectEqual(fieldNum(want, "pointSize", line_defaults.point_size), got.point_size);
    try testing.expectEqualStrings(fieldStr(want, "style", line_defaults.style), got.style);
    const locked = fx.member(want, "locked") orelse std.json.Value{ .bool = line_defaults.locked };
    try testing.expectEqual(locked.bool, got.locked);
    try testing.expectEqualStrings(fieldStr(want, "fillColor", line_defaults.fill_color), got.fill_color);
    try testing.expectEqualStrings(fieldStr(want, "pointColor", line_defaults.point_color), got.point_color);
}

fn expectLines(w: *fx.Walk, name: []const u8, got: []const core.LineDraw, want: std.json.Value) !void {
    w.walked += 1;
    if (want.array.items.len != got.len)
        return w.fail("layout '{s}': want {d} lines, cli parsed {d}\n", .{ name, want.array.items.len, got.len });
    for (want.array.items, got) |expected, g|
        expectLine(g, expected) catch return w.fail("layout '{s}': line mismatch\n", .{name});
}

test "layout corpus: sparse.json — the tolerant parser's per-line defaults" {
    var w = fx.Walk.start();
    defer w.stop();
    const a = w.alloc();

    try w.loadOverrides();
    for (try w.cases("fixtures/layout/sparse.json")) |case| {
        const name = fx.memberStr(case, "name").?;
        const doc = try std.fmt.allocPrint(a, "{{\"lines\":{s}}}", .{try fx.stringify(a, fx.member(case, "sparse").?)});
        var L = try layout.parse(testing.allocator, doc);
        defer L.deinit();

        var want = fx.member(case, "expectFilled").?;
        if (w.override("layout", name)) |ov| {
            if (fx.member(ov, "cliLines")) |cl| want = cl;
        }
        try expectLines(&w, name, L.lines, want);
    }
    try w.report("layout sparse");
}

test "layout corpus: payload.json — exported payloads through the cli parse side" {
    var w = fx.Walk.start();
    defer w.stop();
    const a = w.alloc();

    try w.loadOverrides();
    for (try w.cases("fixtures/layout/payload.json")) |case| {
        const name = fx.memberStr(case, "name").?;
        const payload = fx.member(case, "expectPayload").?;
        var L = try layout.parse(testing.allocator, try fx.stringify(a, payload));
        defer L.deinit();

        // Dimensions + page metadata read with the cli's typing.
        if (fx.member(payload, "imageWidth") != null) {
            try testing.expectEqual(fieldNum(payload, "imageWidth", 0), L.image_width.?);
        } else try testing.expect(L.image_width == null);
        if (fx.memberStr(payload, "pageSize")) |ps| {
            try testing.expectEqualStrings(ps, L.page_size.?);
        } else try testing.expect(L.page_size == null);
        try testing.expectEqual(fieldNum(payload, "customPageWidth", 0), L.custom_page_w);
        try testing.expectEqual(fieldNum(payload, "customPageHeight", 0), L.custom_page_h);

        // Read-both since Phase 6: the corpus's canonical "imageFilter" is what the
        // cli surfaces (string-typed values only, like the browser export).
        if (fx.memberStr(payload, "imageFilter")) |f| {
            try testing.expectEqualStrings(f, L.filter.?);
        } else try testing.expect(L.filter == null);

        // Lines through the same comparator; cli typing drift rides the override.
        var want = fx.member(payload, "lines");
        if (w.override("layout", name)) |ov| {
            if (fx.member(ov, "cliLines")) |cl| want = cl;
        }
        if (want) |lines| {
            try expectLines(&w, name, L.lines, lines);
        } else {
            try testing.expectEqual(@as(usize, 0), L.lines.len);
        }
    }
    try w.report("layout payload");
}

test "layout filter key: read-both since Phase 6, canonical imageFilter wins" {
    var canonical = try layout.parse(testing.allocator, "{\"imageFilter\":\"bw\",\"lines\":[]}");
    defer canonical.deinit();
    try testing.expectEqualStrings("bw", canonical.filter.?);

    var legacy = try layout.parse(testing.allocator, "{\"filter\":\"sepia\",\"lines\":[]}");
    defer legacy.deinit();
    try testing.expectEqualStrings("sepia", legacy.filter.?);

    var both = try layout.parse(testing.allocator, "{\"filter\":\"sepia\",\"imageFilter\":\"bw\",\"lines\":[]}");
    defer both.deinit();
    try testing.expectEqualStrings("bw", both.filter.?);
}

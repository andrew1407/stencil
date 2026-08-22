// Walks the shared layout conformance vectors (browser/js/config/fixtures/layout/)
// against the cli's real tolerant reader (layout.parse — the path /apply and --layout
// use). The cli consumes exported payloads, so sparse.json is walked to expectFilled
// (the tolerant-parser contract) and payload.json through the parse side.
//
// Measured cli drift, pinned via fixture_overrides.json (`cliLines` replaces the
// corpus expectation where the cli's tolerant reader disagrees):
//  · a line whose points read as EMPTY is skipped entirely (the browser keeps it);
//  · every point OBJECT is kept, each missing/mis-typed coordinate defaulting to 0
//    (the browser drops junk points and coerces numeric strings);
//  · a numeric-string field ("3.5") falls back to the default (browser coerces);
//  · non-bool locked falls back to false (browser applies truthiness).
// The top-level filter reads BOTH keys since Phase 6: canonical "imageFilter"
// (the corpus/browser export key) wins over the legacy "filter" spelling.
const std = @import("std");
const layout = @import("../src/layout.zig");
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

fn expectLines(name: []const u8, got: []const core.LineDraw, want: std.json.Value) !usize {
    if (want.array.items.len != got.len) {
        std.debug.print("layout '{s}': want {d} lines, cli parsed {d}\n", .{ name, want.array.items.len, got.len });
        return 1;
    }
    for (want.array.items, got) |w, g| {
        expectLine(g, w) catch {
            std.debug.print("layout '{s}': line mismatch\n", .{name});
            return 1;
        };
    }
    return 0;
}

test "layout corpus: sparse.json — the tolerant parser's per-line defaults" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    const overrides = try fx.parseOverrides(a);
    const cases = try fx.loadJson(a, io, "fixtures/layout/sparse.json");
    var failures: usize = 0;

    for (cases.array.items) |case| {
        const name = fx.memberStr(case, "name").?;
        const doc = try std.fmt.allocPrint(a, "{{\"lines\":{s}}}", .{try fx.stringify(a, fx.member(case, "sparse").?)});
        var L = try layout.parse(testing.allocator, doc);
        defer L.deinit();

        var want = fx.member(case, "expectFilled").?;
        if (fx.overrideFor(overrides, "layout", name)) |ov| {
            if (fx.member(ov, "cliLines")) |cl| want = cl;
        }
        failures += try expectLines(name, L.lines, want);
    }
    try testing.expectEqual(@as(usize, 0), failures);
}

test "layout corpus: payload.json — exported payloads through the cli parse side" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    const overrides = try fx.parseOverrides(a);
    const cases = try fx.loadJson(a, io, "fixtures/layout/payload.json");
    var failures: usize = 0;

    for (cases.array.items) |case| {
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
        if (fx.overrideFor(overrides, "layout", name)) |ov| {
            if (fx.member(ov, "cliLines")) |cl| want = cl;
        }
        if (want) |w| {
            failures += try expectLines(name, L.lines, w);
        } else {
            try testing.expectEqual(@as(usize, 0), L.lines.len);
        }
    }
    try testing.expectEqual(@as(usize, 0), failures);
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

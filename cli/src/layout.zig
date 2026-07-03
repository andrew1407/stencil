//! Layout JSON parsing. The schema mirrors the browser's exported layout
//! (browser/js/core/layout.js → buildLayoutPayload): { imageWidth, imageHeight, lines }
//! where each line matches core models.hpp (points, color, thickness, markerSize,
//! style, locked, fillColor). An optional top-level "filter" is honoured unless the
//! CLI's --filter overrides it; an optional "pageSize" (+ custom cm dims) is surfaced
//! so the wrote line can report the page the layout targets. Everything is owned by
//! an internal arena.
const std = @import("std");
const core = @import("core.zig");

pub const Layout = struct {
    arena: std.heap.ArenaAllocator,
    image_width: ?f64 = null,
    image_height: ?f64 = null,
    filter: ?[]const u8 = null,
    page_size: ?[]const u8 = null, // top-level "pageSize": a named format ("A0".."C10") or "custom"
    custom_page_w: f64 = 0, // "customPageWidth"/"customPageHeight" in cm; 0 = unset
    custom_page_h: f64 = 0,
    lines: []core.LineDraw = &.{},

    pub fn deinit(self: *Layout) void {
        self.arena.deinit();
    }
};

fn asF64(v: std.json.Value, default: f64) f64 {
    return switch (v) {
        .float => |f| f,
        .integer => |i| @floatFromInt(i),
        else => default,
    };
}

fn asBool(v: std.json.Value, default: bool) bool {
    return switch (v) {
        .bool => |b| b,
        else => default,
    };
}

fn fieldF64(obj: std.json.ObjectMap, key: []const u8, default: f64) f64 {
    return if (obj.get(key)) |v| asF64(v, default) else default;
}

fn fieldStrZ(a: std.mem.Allocator, obj: std.json.ObjectMap, key: []const u8, default: []const u8) ![:0]const u8 {
    if (obj.get(key)) |v| {
        if (v == .string) return a.dupeZ(u8, v.string);
    }
    return a.dupeZ(u8, default);
}

pub fn parse(gpa: std.mem.Allocator, bytes: []const u8) !Layout {
    var layout = Layout{ .arena = std.heap.ArenaAllocator.init(gpa) };
    errdefer layout.arena.deinit();
    const a = layout.arena.allocator();

    const root = try std.json.parseFromSliceLeaky(std.json.Value, a, bytes, .{});
    if (root != .object) return error.InvalidLayout;
    const obj = root.object;

    if (obj.get("imageWidth")) |v| layout.image_width = asF64(v, 0);
    if (obj.get("imageHeight")) |v| layout.image_height = asF64(v, 0);
    if (obj.get("filter")) |v| {
        if (v == .string) layout.filter = try a.dupeZ(u8, v.string);
    }
    if (obj.get("pageSize")) |v| {
        if (v == .string) layout.page_size = try a.dupeZ(u8, v.string);
    }
    layout.custom_page_w = fieldF64(obj, "customPageWidth", 0);
    layout.custom_page_h = fieldF64(obj, "customPageHeight", 0);

    var lines: std.ArrayList(core.LineDraw) = .empty;
    if (obj.get("lines")) |lines_v| {
        if (lines_v == .array) {
            for (lines_v.array.items) |line_v| {
                if (line_v != .object) continue;
                const lo = line_v.object;

                var pts: std.ArrayList(f64) = .empty;
                if (lo.get("points")) |pv| {
                    if (pv == .array) {
                        for (pv.array.items) |pt| {
                            if (pt != .object) continue;
                            const po = pt.object;
                            try pts.append(a, fieldF64(po, "x", 0));
                            try pts.append(a, fieldF64(po, "y", 0));
                        }
                    }
                }
                if (pts.items.len == 0) continue;

                try lines.append(a, .{
                    .points = try pts.toOwnedSlice(a),
                    .color = try fieldStrZ(a, lo, "color", "#FFFF00"),
                    .thickness = fieldF64(lo, "thickness", 2),
                    .marker_size = fieldF64(lo, "markerSize", 4),
                    .style = try fieldStrZ(a, lo, "style", "solid"),
                    .locked = if (lo.get("locked")) |v| asBool(v, false) else false,
                    .fill_color = try fieldStrZ(a, lo, "fillColor", "transparent"),
                });
            }
        }
    }
    layout.lines = try lines.toOwnedSlice(a);
    return layout;
}

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

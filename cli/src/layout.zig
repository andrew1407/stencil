//! Layout JSON parsing. The schema mirrors the browser's exported layout (browser/js/core/layout.js →
//! buildLayoutPayload): { imageWidth, imageHeight, lines }, each line matching core models.hpp
//! (points, color, thickness, pointSize, style, locked, fillColor). An optional "imageFilter" (legacy
//! "filter" is still read, canonical wins) is honoured unless --filter overrides it; an optional
//! "pageSize" is surfaced so the wrote line can report the page. Owned by an internal arena.
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
    // Canonical "imageFilter" wins over the legacy "filter" spelling.
    if (obj.get("imageFilter") orelse obj.get("filter")) |v| {
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
                    .point_size = fieldF64(lo, "pointSize", 4),
                    .style = try fieldStrZ(a, lo, "style", "solid"),
                    .locked = if (lo.get("locked")) |v| asBool(v, false) else false,
                    .fill_color = try fieldStrZ(a, lo, "fillColor", "transparent"),
                    // Absent (every layout predating the field) → "" → points inherit
                    // `color`, matching core's Line::pointColor / pointColorOr.
                    .point_color = try fieldStrZ(a, lo, "pointColor", ""),
                });
            }
        }
    }
    layout.lines = try lines.toOwnedSlice(a);
    return layout;
}

// Source→current frame mapping (llm-contract.md §1): op-plan coordinates are in the frame of the
// snapshot the model saw, so a preceding crop/rotate re-maps and clamps the layout's points.

/// One frame-changing edit, in application order: a crop subtracts its RESOLVED origin;
/// a rotate applies clockwise quarter-turns of the pre-rotate `w`×`h` frame.
pub const FrameStep = union(enum) {
    crop: struct { x: f64, y: f64 },
    rotate: struct { quarters: i32, w: f64, h: f64 },
};

pub const Point = struct { x: f64, y: f64 };

/// Map a point through the steps in order. One clockwise quarter-turn of a `w`×`h` frame sends (x, y)
/// to (h − y, x) — the continuous twin of core rotateImageRGBA's pixel mapping (h−1−y, x).
pub fn mapPoint(steps: []const FrameStep, p: Point) Point {
    var out = p;
    for (steps) |s| switch (s) {
        .crop => |cr| {
            out.x -= cr.x;
            out.y -= cr.y;
        },
        .rotate => |r| {
            var w = r.w;
            var h = r.h;
            var q = core.normalizeQuarters(r.quarters);
            while (q > 0) : (q -= 1) {
                // Compute into temps first — a struct literal that reads `out` would
                // alias the in-place write (same trap as session's rotateRectQuarters).
                const nx = h - out.y;
                const ny = out.x;
                out = .{ .x = nx, .y = ny };
                const t = w;
                w = h;
                h = t;
            }
        },
    };
    return out;
}

/// Clamp a point into a `w`×`h` image's bounds ([0,w]×[0,h] — the same clamp the
/// browser's mapPointsHome uses).
pub fn clampPoint(p: Point, w: usize, h: usize) Point {
    return .{
        .x = std.math.clamp(p.x, 0, @as(f64, @floatFromInt(w))),
        .y = std.math.clamp(p.y, 0, @as(f64, @floatFromInt(h))),
    };
}

/// Map + clamp a flat x,y pair slice in place (a LineDraw's points).
pub fn remapPoints(pts: []f64, steps: []const FrameStep, w: usize, h: usize) void {
    var i: usize = 0;
    while (i + 1 < pts.len) : (i += 2) {
        const p = clampPoint(mapPoint(steps, .{ .x = pts[i], .y = pts[i + 1] }), w, h);
        pts[i] = p.x;
        pts[i + 1] = p.y;
    }
}

/// Re-map every `points` entry of a JSON lines ARRAY string through `steps` and clamp into `w`×`h`
/// (identity steps still clamp). Malformed input comes back as an owned copy unchanged.
pub fn remapLinesArrayAlloc(gpa: std.mem.Allocator, lines_json: []const u8, steps: []const FrameStep, w: usize, h: usize) error{OutOfMemory}![]u8 {
    var parsed = std.json.parseFromSlice(std.json.Value, gpa, lines_json, .{}) catch return gpa.dupe(u8, lines_json);
    defer parsed.deinit();
    if (parsed.value != .array) return gpa.dupe(u8, lines_json);
    for (parsed.value.array.items) |line_v| remapLineValue(line_v, steps, w, h);
    return std.json.Stringify.valueAlloc(gpa, parsed.value, .{}) catch return error.OutOfMemory;
}

/// Re-map the `lines` of a full layout DOCUMENT string ({"lines":[…], …}) the same way,
/// keeping every other field. Caller owns the result.
pub fn remapLayoutDocAlloc(gpa: std.mem.Allocator, doc_json: []const u8, steps: []const FrameStep, w: usize, h: usize) error{OutOfMemory}![]u8 {
    var parsed = std.json.parseFromSlice(std.json.Value, gpa, doc_json, .{}) catch return gpa.dupe(u8, doc_json);
    defer parsed.deinit();
    if (parsed.value != .object) return gpa.dupe(u8, doc_json);
    if (parsed.value.object.get("lines")) |lv| {
        if (lv == .array) for (lv.array.items) |line_v| remapLineValue(line_v, steps, w, h);
    }
    return std.json.Stringify.valueAlloc(gpa, parsed.value, .{}) catch return error.OutOfMemory;
}

/// Map + clamp one JSON line object's points in place (shared by the two Alloc variants).
fn remapLineValue(line_v: std.json.Value, steps: []const FrameStep, w: usize, h: usize) void {
    if (line_v != .object) return;
    const pv = line_v.object.get("points") orelse return;
    if (pv != .array) return;
    for (pv.array.items) |pt_v| {
        if (pt_v != .object) continue;
        const xp = pt_v.object.getPtr("x") orelse continue;
        const yp = pt_v.object.getPtr("y") orelse continue;
        const p = clampPoint(mapPoint(steps, .{ .x = jsonF64(xp.*), .y = jsonF64(yp.*) }), w, h);
        xp.* = numValue(p.x);
        yp.* = numValue(p.y);
    }
}

fn jsonF64(v: std.json.Value) f64 {
    return switch (v) {
        .float => |f| f,
        .integer => |i| @floatFromInt(i),
        else => 0,
    };
}

/// An integral value serializes back as a JSON integer (so untouched points round-trip
/// byte-identical and mapped ones stay readable).
fn numValue(v: f64) std.json.Value {
    if (v == @floor(v) and @abs(v) < 1e15) return .{ .integer = @intFromFloat(v) };
    return .{ .float = v };
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
    const out = try remapLinesArrayAlloc(a,
        "[{\"points\":[{\"x\":150,\"y\":50},{\"x\":90,\"y\":20}],\"color\":\"red\",\"thickness\":3}]", &steps, 100, 50);
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
    const out = try remapLayoutDocAlloc(a,
        "{\"filter\":\"bw\",\"lines\":[{\"points\":[{\"x\":9,\"y\":2}]}]}", &steps, 8, 12);
    defer a.free(out);
    try testing.expect(std.mem.indexOf(u8, out, "{\"x\":1,\"y\":2}") != null);
    try testing.expect(std.mem.indexOf(u8, out, "\"filter\":\"bw\"") != null);
}

//! The lines/layout JSON the session round-trips: pulling the lines array out of a layout
//! document, combining two, reading a layout back into an `EditState`, and the typed member
//! lookups those need.
const std = @import("std");
const image = @import("../../media/image.zig");
const core = @import("../../core.zig");
const layout_mod = @import("../../media/layout.zig");
const EditState = @import("../session.zig").EditState;

pub fn rasterizeLinesJson(gpa: std.mem.Allocator, img: *image.Rgba8, lines_json: []const u8) void {
    const wrapped = std.fmt.allocPrint(gpa, "{{\"lines\":{s}}}", .{lines_json}) catch return;
    defer gpa.free(wrapped);
    var parsed = layout_mod.parse(gpa, wrapped) catch return;
    defer parsed.deinit();
    for (parsed.lines) |line| {
        core.rasterizeLine(img.pixels, @intCast(img.width), @intCast(img.height), line);
    }
}

/// Extract the `lines` array of a layout JSON document as an owned JSON array string ("[]" if
/// absent). Caller owns the result.
pub fn extractLinesJson(gpa: std.mem.Allocator, layout_bytes: []const u8) ![]u8 {
    var parsed = std.json.parseFromSlice(std.json.Value, gpa, layout_bytes, .{}) catch return gpa.dupe(u8, "[]");
    defer parsed.deinit();
    if (parsed.value == .object) {
        if (parsed.value.object.get("lines")) |lv| {
            if (lv == .array) return std.json.Stringify.valueAlloc(gpa, lv, .{});
        }
    }
    return gpa.dupe(u8, "[]");
}

/// `/apply <src> combine`: two JSON array strings ("[...]") concatenated, duplicates kept, as the
/// browser's layoutInstall combine joins them, cut at the layout caps on every draw. Caller owns it.
pub fn combineLinesJson(gpa: std.mem.Allocator, a: []const u8, b: []const u8) ![]u8 {
    const ai = innerArray(a);
    const bi = innerArray(b);
    if (ai.len == 0) return gpa.dupe(u8, if (bi.len == 0) "[]" else b);
    if (bi.len == 0) return gpa.dupe(u8, a);
    return std.fmt.allocPrint(gpa, "[{s},{s}]", .{ ai, bi });
}

/// The contents between the outermost `[` `]` of a JSON array string, trimmed (empty if none).
pub fn innerArray(s: []const u8) []const u8 {
    const t = std.mem.trim(u8, s, " \t\r\n");
    if (t.len < 2 or t[0] != '[' or t[t.len - 1] != ']') return "";
    return std.mem.trim(u8, t[1 .. t.len - 1], " \t\r\n");
}

/// `n` clockwise quarter-turns of every point in a JSON lines array inside the pre-turn `w`×`h` view,
/// unclamped: core::rotateLinePointsQuarter's map (layout.mapPoint). Caller owns the result.
pub fn turnLinesJson(gpa: std.mem.Allocator, lines_json: []const u8, n: i32, w: i32, h: i32) ![]u8 {
    return mapLinesJson(gpa, lines_json, .{ .turn = .{ .quarters = core.normalizeQuarters(n), .w = @floatFromInt(w), .h = @floatFromInt(h) } });
}

/// The lines as the browser's crop recalcs them when window `old` becomes `new`: an album/portrait
/// flip clears them, any other resize scales every point (browser scaleLinePoints). Caller owns it.
pub fn recropLinesJson(gpa: std.mem.Allocator, lines_json: []const u8, old: core.Rect, new: core.Rect) ![]u8 {
    const change = core.cropChange(old, new);
    if (change.orientation_changed) return gpa.dupe(u8, "[]");
    if (change.scale == 1) return gpa.dupe(u8, lines_json);
    return mapLinesJson(gpa, lines_json, .{ .scale = change.scale });
}

const PointMap = union(enum) {
    turn: struct { quarters: i32, w: f64, h: f64 },
    scale: f64,

    // Three clockwise quarters are the browser's one left press: (x, y) → (y, W − x), exact.
    fn apply(self: PointMap, p: layout_mod.Point) layout_mod.Point {
        return switch (self) {
            .scale => |s| .{ .x = p.x * s, .y = p.y * s },
            .turn => |t| if (t.quarters == 3) .{ .x = p.y, .y = t.w - p.x } else layout_mod.mapPoint(&.{.{ .rotate = .{ .quarters = t.quarters, .w = t.w, .h = t.h } }}, p),
        };
    }
};

fn mapLinesJson(gpa: std.mem.Allocator, lines_json: []const u8, m: PointMap) ![]u8 {
    var parsed = std.json.parseFromSlice(std.json.Value, gpa, lines_json, .{}) catch return gpa.dupe(u8, lines_json);
    defer parsed.deinit();
    if (parsed.value != .array) return gpa.dupe(u8, lines_json);
    for (parsed.value.array.items) |line_v| {
        if (line_v != .object) continue;
        const pv = line_v.object.get("points") orelse continue;
        if (pv != .array) continue;
        for (pv.array.items) |pt_v| {
            if (pt_v != .object) continue;
            const xp = pt_v.object.getPtr("x") orelse continue;
            const yp = pt_v.object.getPtr("y") orelse continue;
            const p = m.apply(.{ .x = numOf(xp.*), .y = numOf(yp.*) });
            xp.* = jsonOfNum(p.x);
            yp.* = jsonOfNum(p.y);
        }
    }
    return std.json.Stringify.valueAlloc(gpa, parsed.value, .{});
}

fn numOf(v: std.json.Value) f64 {
    return switch (v) {
        .integer => |i| @floatFromInt(i),
        .float => |f| f,
        else => 0,
    };
}

// An integral point stays a JSON integer, so a turned layout reads as the browser writes it.
fn jsonOfNum(v: f64) std.json.Value {
    if (v == @floor(v) and @abs(v) < 1e15) return .{ .integer = @intFromFloat(v) };
    return .{ .float = v };
}

/// Read a server layout document into an EditState (rotation, crop, filter, lines).
pub fn parseLayoutInto(gpa: std.mem.Allocator, layout_bytes: []const u8, out: *EditState) !void {
    out.lines_json = try extractLinesJson(gpa, layout_bytes);
    var parsed = std.json.parseFromSlice(std.json.Value, gpa, layout_bytes, .{}) catch return;
    defer parsed.deinit();
    if (parsed.value != .object) return;
    const obj = parsed.value.object;
    if (jsonStr(obj, "imageFilter")) |m| out.filter_mode = try gpa.dupe(u8, m);
    if (jsonStr(obj, "filterColor")) |c| out.filter_color = try gpa.dupe(u8, c);
    if (jsonInt(obj, "rotationQuarters")) |r| out.rotation = core.normalizeQuarters(@intCast(r));
    if (obj.get("cropRect")) |cv| {
        if (cv == .object) {
            const co = cv.object;
            out.crop = .{
                .x = @intFromFloat(jsonNum(co, "x")),
                .y = @intFromFloat(jsonNum(co, "y")),
                .w = @intFromFloat(jsonNum(co, "width")),
                .h = @intFromFloat(jsonNum(co, "height")),
            };
        }
    }
}

pub fn jsonStr(obj: std.json.ObjectMap, key: []const u8) ?[]const u8 {
    if (obj.get(key)) |v| {
        if (v == .string) return v.string;
    }
    return null;
}

pub fn jsonInt(obj: std.json.ObjectMap, key: []const u8) ?i64 {
    if (obj.get(key)) |v| {
        return switch (v) {
            .integer => |i| i,
            .float => |f| @intFromFloat(f),
            else => null,
        };
    }
    return null;
}

pub fn jsonNum(obj: std.json.ObjectMap, key: []const u8) f64 {
    if (obj.get(key)) |v| {
        return switch (v) {
            .integer => |i| @floatFromInt(i),
            .float => |f| f,
            else => 0,
        };
    }
    return 0;
}

const testing = std.testing;

test "combineLinesJson concatenates arrays, keeps duplicates, handles empties" {
    const a = testing.allocator;
    const m1 = try combineLinesJson(a, "[{\"a\":1}]", "[{\"b\":2}]");
    defer a.free(m1);
    try testing.expectEqualStrings("[{\"a\":1},{\"b\":2}]", m1);
    const m2 = try combineLinesJson(a, "[]", "[{\"b\":2}]");
    defer a.free(m2);
    try testing.expectEqualStrings("[{\"b\":2}]", m2);
    const m3 = try combineLinesJson(a, "[{\"a\":1}]", "[]");
    defer a.free(m3);
    try testing.expectEqualStrings("[{\"a\":1}]", m3);
    const m4 = try combineLinesJson(a, "[{\"a\":1}]", "[{\"a\":1}]");
    defer a.free(m4);
    try testing.expectEqualStrings("[{\"a\":1},{\"a\":1}]", m4);
}
test "turnLinesJson turns points as core::rotateLinePointsQuarter does, without clamping" {
    const a = testing.allocator;
    const src = "[{\"points\":[{\"x\":1,\"y\":2},{\"x\":20,\"y\":-1.5}],\"color\":\"#0f0\"}]";
    // Right in 16x12: (x, y) → (H − y, x); left: (x, y) → (y, W − x).
    const right = try turnLinesJson(a, src, 1, 16, 12);
    defer a.free(right);
    try testing.expectEqualStrings("[{\"points\":[{\"x\":10,\"y\":1},{\"x\":13.5,\"y\":20}],\"color\":\"#0f0\"}]", right);
    const left = try turnLinesJson(a, src, -1, 16, 12);
    defer a.free(left);
    try testing.expectEqualStrings("[{\"points\":[{\"x\":2,\"y\":15},{\"x\":-1.5,\"y\":-4}],\"color\":\"#0f0\"}]", left);
    const bad = try turnLinesJson(a, "not json", 1, 16, 12);
    defer a.free(bad);
    try testing.expectEqualStrings("not json", bad);
    const tenth = try turnLinesJson(a, "[{\"points\":[{\"x\":2,\"y\":0.1}]}]", -1, 16, 12);
    defer a.free(tenth);
    try testing.expectEqualStrings("[{\"points\":[{\"x\":0.1,\"y\":14}]}]", tenth); // one press, no drift
}

test "recropLinesJson scales the lines on a resize and clears them on an album/portrait flip" {
    const a = testing.allocator;
    const src = "[{\"points\":[{\"x\":4,\"y\":5},{\"x\":3,\"y\":2}],\"thickness\":2}]";
    const album = core.Rect{ .x = 0, .y = 0, .w = 16, .h = 12 };
    const half = try recropLinesJson(a, src, album, .{ .x = 4, .y = 3, .w = 8, .h = 6 });
    defer a.free(half);
    try testing.expectEqualStrings("[{\"points\":[{\"x\":2,\"y\":2.5},{\"x\":1.5,\"y\":1}],\"thickness\":2}]", half);
    const moved = try recropLinesJson(a, src, .{ .x = 0, .y = 0, .w = 8, .h = 6 }, .{ .x = 3, .y = 2, .w = 8, .h = 6 });
    defer a.free(moved);
    try testing.expectEqualStrings(src, moved);
    const flipped = try recropLinesJson(a, src, album, .{ .x = 0, .y = 0, .w = 8, .h = 12 });
    defer a.free(flipped);
    try testing.expectEqualStrings("[]", flipped);
}

test "extractLinesJson pulls the lines array, defaults to []" {
    const a = testing.allocator;
    const l = try extractLinesJson(a, "{\"lines\":[{\"color\":\"#f00\"}],\"imageFilter\":\"bw\"}");
    defer a.free(l);
    try testing.expectEqualStrings("[{\"color\":\"#f00\"}]", l);
    const none = try extractLinesJson(a, "{\"imageFilter\":\"bw\"}");
    defer a.free(none);
    try testing.expectEqualStrings("[]", none);
}

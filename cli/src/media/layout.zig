//! Layout JSON parsing. The schema mirrors the browser's exported layout (browser/js/core/layout.js →
//! buildLayoutPayload): { imageWidth, imageHeight, lines }, each line matching core models.hpp
//! (points, color, thickness, pointSize, style, locked, fillColor). An optional "imageFilter" (legacy
//! "filter" is still read, canonical wins) is honoured unless --filter overrides it; an optional
//! "pageSize" is surfaced so the wrote line can report the page. Owned by an internal arena.
const std = @import("std");
const core = @import("../core.zig");
const lines_mod = @import("layout/lines.zig");

const asF64 = lines_mod.asF64;
const fieldF64 = lines_mod.fieldF64;
pub const lineOf = lines_mod.lineOf;

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

pub fn parse(gpa: std.mem.Allocator, bytes: []const u8) !Layout {
    return parseCapped(gpa, bytes, core.layoutCaps());
}

/// `parse` under explicit caps, so a test can reach them without a million-point document.
pub fn parseCapped(gpa: std.mem.Allocator, bytes: []const u8, caps: core.LayoutCaps) !Layout {
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

    layout.lines = try lines_mod.readLines(a, obj.get("lines"), caps);
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
                // alias the in-place write.
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

test {
    _ = lines_mod;
}

//! A layout's `lines` array read into drawable `core.LineDraw`s, under the browser's caps
//! (browser/js/core/layout.js sanitizeLines, core.layoutCaps): each line keeps at most the
//! per-line cap, and the line that spends the last of the total is cut there, every one after
//! it dropped. What a line or point reads as stays the cli's own (layout_fixtures_test.zig).
const std = @import("std");
const core = @import("../../core.zig");

pub fn asF64(v: std.json.Value, default: f64) f64 {
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

pub fn fieldF64(obj: std.json.ObjectMap, key: []const u8, default: f64) f64 {
    return if (obj.get(key)) |v| asF64(v, default) else default;
}

fn fieldStrZ(a: std.mem.Allocator, obj: std.json.ObjectMap, key: []const u8, default: []const u8) ![:0]const u8 {
    if (obj.get(key)) |v| {
        if (v == .string) return a.dupeZ(u8, v.string);
    }
    return a.dupeZ(u8, default);
}

/// One line object with at most `max_points` of its points, every point object kept (a
/// missing coordinate reads 0) and each absent field at its default.
pub fn lineOf(a: std.mem.Allocator, lo: std.json.ObjectMap, max_points: usize) !core.LineDraw {
    var pts: std.ArrayList(f64) = .empty;
    if (lo.get("points")) |pv| {
        if (pv == .array) {
            for (pv.array.items) |pt| {
                if (pts.items.len / 2 >= max_points) break;
                if (pt != .object) continue;
                const po = pt.object;
                try pts.append(a, fieldF64(po, "x", 0));
                try pts.append(a, fieldF64(po, "y", 0));
            }
        }
    }
    return .{
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
    };
}

/// A line a user-named layout may not carry: `points` that is not an array, or a thickness or
/// pointSize that is a negative or non-finite number (opRegistry.json's layout rule: >= 0).
pub const BadLine = struct {
    index: usize,
    field: []const u8,

    /// "lines[2].thickness must be a number >= 0", the registry's wording for the same rule.
    pub fn describe(self: BadLine, buf: []u8) []const u8 {
        const want = if (std.mem.eql(u8, self.field, "points")) "must be an array" else "must be a number >= 0";
        return std.fmt.bufPrint(buf, "lines[{d}].{s} {s}", .{ self.index, self.field, want }) catch self.field;
    }
};

/// The first such line in a `lines` value; a non-number field stays the tolerant default.
pub fn firstBadLine(lines_v: ?std.json.Value) ?BadLine {
    const v = lines_v orelse return null;
    if (v != .array) return null;
    for (v.array.items, 0..) |line_v, i| {
        if (line_v != .object) continue;
        const lo = line_v.object;
        if (lo.get("points")) |pv| if (pv != .array) return .{ .index = i, .field = "points" };
        for ([_][]const u8{ "thickness", "pointSize" }) |key| {
            const n = asF64(lo.get(key) orelse continue, 0);
            if (n < 0 or !std.math.isFinite(n)) return .{ .index = i, .field = key };
        }
    }
    return null;
}

/// The drawable lines of a `lines` value under `caps`. A line object with no points still
/// counts against the line cap, as the browser keeps it, but draws nothing and is left out.
pub fn readLines(a: std.mem.Allocator, lines_v: ?std.json.Value, caps: core.LayoutCaps) ![]core.LineDraw {
    var lines: std.ArrayList(core.LineDraw) = .empty;
    const v = lines_v orelse return lines.toOwnedSlice(a);
    if (v != .array) return lines.toOwnedSlice(a);
    var counted: usize = 0;
    var budget = caps.points;
    for (v.array.items) |line_v| {
        if (counted >= caps.lines or budget == 0) break;
        if (line_v != .object) continue;
        counted += 1;
        const line = try lineOf(a, line_v.object, @min(caps.line_points, budget));
        budget -= line.points.len / 2;
        if (line.points.len != 0) try lines.append(a, line);
    }
    return lines.toOwnedSlice(a);
}

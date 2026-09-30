//! One lowered op as one action in the op-plan vocabulary of
//! common/config/llm/opRegistry.json — the same wire names `applyPlanAction` executes.
//! Pure builders: which actions a block needs, and in what frame, is sequence.zig's.
const std = @import("std");

const core = @import("../../core.zig");

const decode = @import("../decode.zig");

const Value = std.json.Value;
const ObjectMap = std.json.ObjectMap;
const Array = std.json.Array;

fn numValue(v: f64) Value {
    if (v == @floor(v) and @abs(v) < 1e15) return .{ .integer = @intFromFloat(v) };
    return .{ .float = v };
}

fn opObject(a: std.mem.Allocator, name: []const u8) !ObjectMap {
    var m: ObjectMap = .empty;
    try m.put(a, "op", .{ .string = name });
    return m;
}

/// A tint as the registry's HEX grammar wants it; the raw token when the core does not know the colour,
/// so the validator rejects it. Line colours are NOT folded — the layout schema carries the CSS names.
fn hex(a: std.mem.Allocator, token: []const u8) []const u8 {
    const z = core.zstr(token) orelse return token;
    const c = core.parseColor(z) orelse return token;
    return std.fmt.allocPrint(a, "#{x:0>2}{x:0>2}{x:0>2}", .{ c.r, c.g, c.b }) catch token;
}

pub fn openAction(a: std.mem.Allocator, target: []const u8, is_url: bool) !Value {
    var m = try opObject(a, if (is_url) "openUrl" else "openFile");
    try m.put(a, if (is_url) "url" else "path", .{ .string = target });
    return .{ .object = m };
}

pub fn filterAction(a: std.mem.Allocator, f: decode.Filter) !Value {
    var m = try opObject(a, "filter");
    try m.put(a, "mode", .{ .string = f.mode });
    if (std.mem.eql(u8, f.mode, "custom")) try m.put(a, "tint", .{ .string = hex(a, f.tint) });
    return .{ .object = m };
}

pub fn numberAction(a: std.mem.Allocator, name: []const u8, key: []const u8, n: i64) !Value {
    var m = try opObject(a, name);
    try m.put(a, key, .{ .integer = n });
    return .{ .object = m };
}

pub fn saveAction(a: std.mem.Allocator, target: []const u8) !Value {
    var m = try opObject(a, "save");
    if (target.len != 0) try m.put(a, "path", .{ .string = target });
    return .{ .object = m };
}

/// One drawn line as a layout line object, its image-pixel points moved by (`dx`, `dy`).
pub fn lineValue(a: std.mem.Allocator, line: core.LineDraw, dx: f64, dy: f64) !Value {
    var pts: Array = .init(a);
    var k: usize = 0;
    while (k + 1 < line.points.len) : (k += 2) {
        var p: ObjectMap = .empty;
        try p.put(a, "x", numValue(line.points[k] + dx));
        try p.put(a, "y", numValue(line.points[k + 1] + dy));
        try pts.append(.{ .object = p });
    }

    var m: ObjectMap = .empty;
    try m.put(a, "points", .{ .array = pts });
    try m.put(a, "color", .{ .string = line.color });
    try m.put(a, "style", .{ .string = line.style });
    try m.put(a, "fillColor", .{ .string = line.fill_color });
    if (line.point_color.len != 0) try m.put(a, "pointColor", .{ .string = line.point_color });
    try m.put(a, "thickness", numValue(line.thickness));
    try m.put(a, "pointSize", numValue(line.point_size));
    try m.put(a, "locked", .{ .bool = line.locked });
    return .{ .object = m };
}

/// A `layout` setting the drawn lines to `set`, every point moved by (`dx`, `dy`).
pub fn linesAction(a: std.mem.Allocator, set: []const core.LineDraw, dx: f64, dy: f64) !Value {
    var lines: Array = .init(a);
    for (set) |line| try lines.append(try lineValue(a, line, dx, dy));
    var m = try opObject(a, "layout");
    try m.put(a, "lines", .{ .array = lines });
    return .{ .object = m };
}

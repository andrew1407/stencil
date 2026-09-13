//! One block's lowered ops, rewritten in the op-plan vocabulary of
//! browser/js/config/llm/opRegistry.json — the same wire names `applyPlanAction` executes.
//! Nothing is fetched or decoded here; lengths resolve against the dims the caller probed.
const std = @import("std");

const core = @import("../core.zig");
const scriptCore = @import("../scriptCore.zig");

const Value = std.json.Value;
const ObjectMap = std.json.ObjectMap;
const Array = std.json.Array;

/// CSS pixels per cm at 96 dpi — the basis apply.zig resolves against, kept identical so a
/// planned crop and a run crop land on the same pixels.
const PX_PER_CM: f64 = 96.0 / 2.54;

/// The image size lengths resolve against; it moves as the block's crops narrow it.
pub const Dims = struct { w: f64, h: f64 };

const MAX_RESOLVE: usize = 2 * (200 + 1); // MAX_POINTS_PER_LINE points + thickness/pointSize

fn numValue(v: f64) Value {
    if (v == @floor(v) and @abs(v) < 1e15) return .{ .integer = @intFromFloat(v) };
    return .{ .float = v };
}

fn opObject(a: std.mem.Allocator, name: []const u8) !ObjectMap {
    var m: ObjectMap = .empty;
    try m.put(a, "op", .{ .string = name });
    return m;
}

/// A tint as the registry's HEX grammar wants it; the raw token when it is not a colour the
/// core knows (the validator then rejects it, which is the honest answer). Line colours are
/// NOT folded this way — "transparent" and the CSS names are what the layout schema carries.
fn hex(a: std.mem.Allocator, token: []const u8) []const u8 {
    const z = core.zstr(token) orelse return token;
    const c = core.parseColor(z) orelse return token;
    return std.fmt.allocPrint(a, "#{x:0>2}{x:0>2}{x:0>2}", .{ c.r, c.g, c.b }) catch token;
}

fn openAction(a: std.mem.Allocator, target: []const u8, is_url: bool) !Value {
    var m = try opObject(a, if (is_url) "openUrl" else "openFile");
    try m.put(a, if (is_url) "url" else "path", .{ .string = target });
    return .{ .object = m };
}

fn cropAction(a: std.mem.Allocator, script: scriptCore.Script, i: u32) !Value {
    var spec: ObjectMap = .empty;
    for ([_][]const u8{ "x1", "x2", "y1", "y2" }, 0..) |key, k| {
        const tok = script.opTok(i, @intCast(k));
        if (tok.len != 0) try spec.put(a, key, .{ .string = tok });
    }
    const aspect = script.opStr(i, 0);
    if (aspect.len != 0) try spec.put(a, "aspect", .{ .string = aspect });
    if ((script.opNum(i, 0) orelse 0) != 0) try spec.put(a, "album", .{ .bool = true });
    var m = try opObject(a, "crop");
    try m.put(a, "spec", .{ .object = spec });
    return .{ .object = m };
}

fn filterAction(a: std.mem.Allocator, script: scriptCore.Script, i: u32) !Value {
    const mode = script.opStr(i, 0);
    var m = try opObject(a, "filter");
    try m.put(a, "mode", .{ .string = mode });
    if (std.mem.eql(u8, mode, "custom")) try m.put(a, "tint", .{ .string = hex(a, script.opStr(i, 1)) });
    return .{ .object = m };
}

fn stepsAction(a: std.mem.Allocator, script: scriptCore.Script, i: u32, name: []const u8) !Value {
    var m = try opObject(a, name);
    const n = script.opNum(i, 0) orelse 1;
    try m.put(a, "steps", .{ .integer = @intFromFloat(@max(1, n)) });
    return .{ .object = m };
}

fn saveAction(a: std.mem.Allocator, script: scriptCore.Script, i: u32) !Value {
    var m = try opObject(a, "save");
    const target = script.opStr(i, 0);
    if (target.len != 0) try m.put(a, "path", .{ .string = target });
    return .{ .object = m };
}

/// One `@line` / `@rect` as a layout line object, its points already in image pixels.
fn lineValue(a: std.mem.Allocator, script: scriptCore.Script, i: u32, d: Dims, locked: bool) !?Value {
    var buf: [MAX_RESOLVE]f64 = undefined;
    const r = script.resolve(i, d.w, d.h, PX_PER_CM, PX_PER_CM, &buf) catch return null;
    if (r.len < 4) return null;

    var pts: Array = .init(a);
    var k: usize = 0;
    while (k + 3 < r.len) : (k += 2) {
        var p: ObjectMap = .empty;
        try p.put(a, "x", numValue(r[k]));
        try p.put(a, "y", numValue(r[k + 1]));
        try pts.append(.{ .object = p });
    }

    var m: ObjectMap = .empty;
    try m.put(a, "points", .{ .array = pts });
    try m.put(a, "color", .{ .string = script.opStr(i, 0) });
    try m.put(a, "style", .{ .string = script.opStr(i, 1) });
    try m.put(a, "fillColor", .{ .string = script.opStr(i, 2) });
    try m.put(a, "thickness", numValue(r[r.len - 2]));
    try m.put(a, "pointSize", numValue(r[r.len - 1]));
    try m.put(a, "locked", .{ .bool = locked });
    return .{ .object = m };
}

/// The actions one block lowers to, in order. `dims` is null when nothing local could be
/// probed — the shape ops are then dropped rather than resolved against a size nobody has.
pub fn build(
    a: std.mem.Allocator,
    script: scriptCore.Script,
    block: scriptCore.Block,
    first_input: []const u8,
    dims: ?Dims,
) ![]Value {
    var out: Array = .init(a);
    var pending: Array = .init(a); // @line / @rect awaiting a flush, exactly as run.zig holds them
    var d = dims;

    var i: u32 = block.op_start;
    const end = block.op_start + block.op_count;
    while (i < end) : (i += 1) {
        const op = script.op(i) orelse continue;
        switch (op.kind) {
            .open => if (first_input.len != 0)
                try out.append(try openAction(a, first_input, block.kind == .url)),
            .frame => {
                var m = try opObject(a, "frame");
                try m.put(a, "index", .{ .integer = @intFromFloat(script.opNum(i, 0) orelse 0) });
                try out.append(.{ .object = m });
            },
            .crop => {
                try flush(a, &out, &pending);
                try out.append(try cropAction(a, script, i));
                if (d) |cur| d = cropDims(script, i, cur);
            },
            .filter => try out.append(try filterAction(a, script, i)),
            .line, .rect => if (d) |cur| {
                if (try lineValue(a, script, i, cur, op.kind == .rect)) |v| try pending.append(v);
            },
            .layout => {
                if (std.mem.eql(u8, script.opStr(i, 1), "replace")) pending.clearRetainingCapacity();
                const kind: scriptCore.SourceKind = @enumFromInt(@as(c_int, @intFromFloat(script.opNum(i, 0) orelse 1)));
                try out.append(try openAction(a, script.opStr(i, 0), kind == .url));
            },
            .save => {
                try flush(a, &out, &pending);
                try out.append(try saveAction(a, script, i));
            },
            .undo => try out.append(try stepsAction(a, script, i, "undo")),
            .redo => try out.append(try stepsAction(a, script, i, "redo")),
        }
    }
    try flush(a, &out, &pending);
    return out.items;
}

/// Burns the accumulated shapes into one `layout` action, the way `@save` burns them into
/// pixels. An empty pending list writes nothing — an empty `lines` array would CLEAR them.
fn flush(a: std.mem.Allocator, out: *Array, pending: *Array) !void {
    if (pending.items.len == 0) return;
    var lines: Array = .init(a);
    try lines.appendSlice(pending.items);
    var m = try opObject(a, "layout");
    try m.put(a, "lines", .{ .array = lines });
    try out.append(.{ .object = m });
    pending.clearRetainingCapacity();
}

fn cropDims(script: scriptCore.Script, i: u32, cur: Dims) Dims {
    var buf: [8]f64 = undefined;
    const r = script.resolve(i, cur.w, cur.h, PX_PER_CM, PX_PER_CM, &buf) catch return cur;
    if (r.len < 4 or r[2] <= 0 or r[3] <= 0) return cur;
    return .{ .w = r[2], .h = r[3] };
}

const testing = std.testing;

test "a block lowers to the registry's own op names" {
    var arena: std.heap.ArenaAllocator = .init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();

    var s = try scriptCore.Script.parse("@source a.png:\n  @crop 10%\n  @rect (1,1) (5,5)\n  @save out.png\n");
    defer s.deinit();
    const acts = try build(a, s, s.block(0).?, "a.png", .{ .w = 200, .h = 100 });

    try testing.expectEqual(@as(usize, 4), acts.len);
    try testing.expectEqualStrings("openFile", acts[0].object.get("op").?.string);
    try testing.expectEqualStrings("crop", acts[1].object.get("op").?.string);
    try testing.expectEqualStrings("10%", acts[1].object.get("spec").?.object.get("x1").?.string);
    try testing.expectEqualStrings("layout", acts[2].object.get("op").?.string);
    try testing.expect(acts[2].object.get("lines").?.array.items[0].object.get("locked").?.bool);
    try testing.expectEqualStrings("save", acts[3].object.get("op").?.string);
    try testing.expectEqualStrings("out.png", acts[3].object.get("path").?.string);
}

test "a custom filter tint is written as hex, and undo carries its steps" {
    var arena: std.heap.ArenaAllocator = .init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();

    var s = try scriptCore.Script.parse("@source a.png:\n  @filter aqua\n  @rect (1,1) (2,2)\n  @undo\n  @save o.png\n");
    defer s.deinit();
    const acts = try build(a, s, s.block(0).?, "a.png", .{ .w = 40, .h = 40 });

    try testing.expectEqualStrings("custom", acts[1].object.get("mode").?.string);
    try testing.expectEqualStrings("#00ffff", acts[1].object.get("tint").?.string);
    var steps: ?i64 = null;
    for (acts) |v| {
        if (std.mem.eql(u8, v.object.get("op").?.string, "undo")) steps = v.object.get("steps").?.integer;
    }
    try testing.expectEqual(@as(?i64, 1), steps);
}

test "without dims the shape ops are dropped rather than resolved against nothing" {
    var arena: std.heap.ArenaAllocator = .init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();

    var s = try scriptCore.Script.parse("@source https://e.example/a.png:\n  @line (0,0) (10%,10%)\n  @save\n");
    defer s.deinit();
    const acts = try build(a, s, s.block(0).?, "https://e.example/a.png", null);
    try testing.expectEqual(@as(usize, 2), acts.len);
    try testing.expectEqualStrings("openUrl", acts[0].object.get("op").?.string);
    try testing.expectEqualStrings("save", acts[1].object.get("op").?.string);
}

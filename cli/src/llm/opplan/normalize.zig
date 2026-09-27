//! Core's normalized action → a typed `Action`. core/opplan already proved every field (and ran the
//! console's surface rules), so these only READ; strings are arena-owned slices of core's result.
const std = @import("std");
const opSchema = @import("../opSchema.zig");
const ObjectMap = opSchema.ObjectMap;
const Value = opSchema.Value;
const model = @import("model.zig");
const Action = model.Action;
const CropEdges = model.CropEdges;
const Dir = model.Dir;
const FilterMode = model.FilterMode;
const findOp = model.findOp;

pub fn strOpt(n: ObjectMap, key: []const u8) ?[]const u8 {
    const v = n.get(key) orelse return null;
    return if (v == .string) v.string else null;
}

pub fn boolOpt(n: ObjectMap, key: []const u8) ?bool {
    const v = n.get(key) orelse return null;
    return if (v == .bool) v.bool else null;
}

pub fn numOpt(n: ObjectMap, key: []const u8) ?f64 {
    return numOf(n.get(key) orelse return null);
}

pub fn numOf(v: Value) ?f64 {
    return switch (v) {
        .integer => |i| @floatFromInt(i),
        .float => |f| f,
        .number_string => |s| std.fmt.parseFloat(f64, s) catch null,
        else => null,
    };
}

/// A validated integer as the executor's index type (an out-of-range value saturates —
/// the executor then reports the missing frame/attachment).
pub fn indexOf(v: Value) u32 {
    return std.math.lossyCast(u32, numOf(v) orelse 0);
}

pub fn fill(a: std.mem.Allocator, name: []const u8, n: ObjectMap) error{OutOfMemory}!Action {
    const d = findOp(name) orelse std.debug.panic("registry.zig has no descriptor for \"{s}\"", .{name});
    switch (d.tag) {
        .crop => {
            const spec = n.get("spec").?.object;
            var edges = CropEdges{};
            inline for (.{ "x1", "x2", "y1", "y2", "aspect" }) |key| @field(edges, key) = strOpt(spec, key);
            edges.album = boolOpt(spec, "album") orelse false;
            return .{ .crop = edges };
        },
        .rotate => return .{ .rotate = .{
            .dir = std.meta.stringToEnum(Dir, strOpt(n, "dir").?).?,
            .times = @intFromFloat(numOpt(n, "times").?),
        } },
        .filter => return .{ .filter = .{
            .mode = std.meta.stringToEnum(FilterMode, strOpt(n, "mode").?).?,
            .tint = strOpt(n, "tint") orelse "",
        } },
        .layout => {
            // Every field validated + unknown keys rejected, so the lines re-serialize into
            // exactly the layout `lines` document the session's /apply path already consumes.
            const json = std.json.Stringify.valueAlloc(a, n.get("lines").?, .{}) catch return error.OutOfMemory;
            return .{ .layout = .{ .lines_json = json } };
        },
        .formula => {
            // §2: `enabled` rides ALONE — the formulas on/off toggle (false restores identity).
            if (boolOpt(n, "enabled")) |enabled| return .{ .formula = .{ .axis = 0, .expr = "", .enabled = enabled } };
            const expr = strOpt(n, "expr").?;
            // An empty (or blank) expression clears that axis (§2).
            return .{ .formula = .{
                .axis = strOpt(n, "axis").?[0],
                .expr = if (std.mem.trim(u8, expr, &std.ascii.whitespace).len == 0) "" else expr,
            } };
        },
        .page => return .{ .page = .{
            .format = strOpt(n, "format") orelse "",
            .width = numOpt(n, "width") orelse 0,
            .height = numOpt(n, "height") orelse 0,
        } },
        .blank => return .{ .blank = .{
            .color = strOpt(n, "color").?,
            .format = strOpt(n, "format"),
            .width = numOpt(n, "width") orelse 0,
            .height = numOpt(n, "height") orelse 0,
        } },
        .frame => {
            if (n.get("index")) |idx| {
                const one = try a.alloc(u32, 1);
                one[0] = indexOf(idx);
                return .{ .frame = .{ .indices = one } };
            }
            const items = n.get("indices").?.array.items;
            const out = try a.alloc(u32, items.len);
            for (items, 0..) |item, i| out[i] = indexOf(item);
            return .{ .frame = .{ .indices = out } };
        },
        .undo => return .{ .undo = .{ .steps = @intFromFloat(numOpt(n, "steps").?) } },
        .redo => return .{ .redo = .{ .steps = @intFromFloat(numOpt(n, "steps").?) } },
        .reset => return .reset,
        .image => return .{ .image = .{ .index = indexOf(n.get("index").?) } },
        .save => return .{ .save = .{ .name = strOpt(n, "name") orelse "", .path = strOpt(n, "path") orelse "" } },
        .accent => return .{ .accent = .{ .color = strOpt(n, "color") orelse "", .preset = strOpt(n, "preset") orelse "" } },
        // Resolution against the user's own servers happens at execution; the
        // console trims the name it will match on.
        .connect => return .{ .connect = .{ .server = std.mem.trim(u8, strOpt(n, "server").?, " \t") } },
        .disconnect => return .{ .disconnect = .{ .server = std.mem.trim(u8, strOpt(n, "server").?, " \t") } },
        .reconnect => return .{ .reconnect = .{ .server = std.mem.trim(u8, strOpt(n, "server").?, " \t") } },
        // The executor applies the SAME guards the /delete command uses (.stencil only,
        // no URLs, no escaping the working directory).
        .delete => return .{ .delete = .{ .path = std.mem.trim(u8, strOpt(n, "path").?, " \t") } },
        // A LOCAL path in a format this app opens (core's pathExtension rule); whether the USER
        // wrote it is checked at execution.
        .open_file => return .{ .open_file = .{ .path = std.mem.trim(u8, strOpt(n, "path").?, " \t") } },
        .open_url => return .{ .open_url = .{ .url = strOpt(n, "url").?, .incognito = boolOpt(n, "incognito") orelse false } },
        .copy => return .copy,
        .clear => return .clear,
        .clear_chat => return .clear_chat,
    }
}

//! Per-value checks in plan/schema.js order: type, then caps, enums, ranges, grammars.
//! An object defers to fields.zig, which recurses back through `checkValue`.
const std = @import("std");
const json = @import("json.zig");
const grammars = @import("grammars.zig");
const ctxm = @import("ctx.zig");
const fields_mod = @import("fields.zig");

const Value = json.Value;
const ObjectMap = json.ObjectMap;
const Error = json.Error;
const Ctx = ctxm.Ctx;
const Path = ctxm.Path;
const label = ctxm.label;
const item = ctxm.item;
const numText = ctxm.numText;
const quoteList = ctxm.quoteList;
const matches = grammars.matches;
const grammar = grammars.grammar;
const numOf = json.numOf;
const isInt = json.isInt;
const getObj = json.getObj;
const getArr = json.getArr;
const getStr = json.getStr;
const getBool = json.getBool;
const strEq = json.strEq;
const inList = json.inList;
const trimWs = json.trimWs;
const checkFields = fields_mod.checkFields;

pub fn checkString(ctx: Ctx, v: Value, spec: ObjectMap, path: Path, parent: ?ObjectMap) Error!void {
    if (v != .string) return ctx.fail("{s} must be a string", .{try label(ctx, path)});
    const max = if (spec.get("maxChars")) |m| ctx.schema.limit(m) else ctx.schema.limitNamed("MAX_STRING_CHARS");
    if (@as(f64, @floatFromInt(v.string.len)) > max) return ctx.fail("{s} is longer than {d} characters", .{ try label(ctx, path), @as(u64, @intFromFloat(max)) });
    const s = if (getBool(spec, "trim")) trimWs(v.string) else v.string;
    if (getBool(spec, "nonEmpty") and trimWs(s).len == 0) return ctx.fail("{s} must be a non-empty string", .{try label(ctx, path)});
    if (spec.get("enum")) |e| {
        if (!inList(.{ .string = s }, e)) return ctx.fail("{s} must be one of {s}", .{ try label(ctx, path), try quoteList(ctx, e) });
    }
    if (spec.get("literals")) |l| {
        if (inList(.{ .string = s }, l)) return;
    }
    if (getBool(spec, "blankOk") and trimWs(s).len == 0) return;
    var names: [8][]const u8 = undefined;
    var n: usize = 0;
    if (getObj(spec, "regexBy")) |by| {
        // The grammar depends on a sibling's value (formula `expr` by `axis`).
        const map = getObj(by, "map").?;
        if (parent) |p| {
            if (p.get(getStr(by, "key").?)) |dep| {
                if (dep == .string) {
                    if (getStr(map, dep.string)) |g| {
                        names[0] = g;
                        n = 1;
                    }
                }
            }
        }
    } else if (spec.get("regex")) |r| switch (r) {
        .string => |g| {
            names[0] = g;
            n = 1;
        },
        .array => |arr| for (arr.items) |g| {
            names[n] = g.string;
            n += 1;
        },
        else => {},
    };
    if (n != 0) {
        var hit = false;
        for (names[0..n]) |g| hit = hit or matches(grammar(g), s);
        if (!hit) {
            var wording: std.ArrayList(u8) = .empty;
            for (names[0..n], 0..) |g, i| {
                if (i != 0) try wording.appendSlice(ctx.a, " or ");
                try wording.appendSlice(ctx.a, ctx.schema.describe(g));
            }
            return ctx.fail("{s} must be {s}", .{ try label(ctx, path), wording.items });
        }
    }
    if (getStr(spec, "regexNot")) |g| {
        if (matches(grammar(g), s)) return ctx.fail("{s} must be a local value, not {s}", .{ try label(ctx, path), ctx.schema.describe(g) });
    }
}

pub fn checkNumber(ctx: Ctx, v: Value, spec: ObjectMap, path: Path) Error!void {
    const integer = strEq(spec.get("type").?, "integer");
    const noun: []const u8 = if (integer) "an integer" else "a number";
    if (!(if (integer) isInt(v) else numOf(v) != null)) return ctx.fail("{s} must be {s}", .{ try label(ctx, path), noun });
    if (spec.get("enum")) |e| {
        if (!inList(v, e)) return ctx.fail("{s} must be one of {s}", .{ try label(ctx, path), try quoteList(ctx, e) });
    }
    if (getArr(spec, "range")) |r| {
        const f = numOf(v).?;
        const lo = numOf(r[0]);
        const hi = numOf(r[1]);
        if ((lo != null and f < lo.?) or (hi != null and f > hi.?)) {
            const range = if (lo != null and hi != null)
                try std.fmt.allocPrint(ctx.a, "{s}..{s}", .{ try numText(ctx, r[0]), try numText(ctx, r[1]) })
            else if (lo != null)
                try std.fmt.allocPrint(ctx.a, ">= {s}", .{try numText(ctx, r[0])})
            else
                try std.fmt.allocPrint(ctx.a, "<= {s}", .{try numText(ctx, r[1])});
            return ctx.fail("{s} must be {s} {s}", .{ try label(ctx, path), noun, range });
        }
    }
}

pub fn checkBoolean(ctx: Ctx, v: Value, spec: ObjectMap, path: Path) Error!void {
    if (v != .bool) return ctx.fail("{s} must be a boolean", .{try label(ctx, path)});
    if (spec.get("enum")) |e| {
        if (!inList(v, e)) return ctx.fail("{s} must be {s}", .{ try label(ctx, path), try quoteList(ctx, e) });
    }
}

pub fn checkArray(ctx: Ctx, v: Value, spec: ObjectMap, path: Path) Error!void {
    if (v != .array) return ctx.fail("{s} must be an array", .{try label(ctx, path)});
    const len: f64 = @floatFromInt(v.array.items.len);
    const min: ?f64 = if (spec.get("minItems")) |m| ctx.schema.limit(m) else null;
    const max: ?f64 = if (spec.get("maxItems")) |m| ctx.schema.limit(m) else null;
    if (min != null and min.? == 1 and len == 0) return ctx.fail("{s} must be a non-empty array", .{try label(ctx, path)});
    const window = min != null and min.? > 1 and max != null; // a real N..M window, not just a cap
    if (max != null and len > max.?) {
        if (window) return ctx.fail("{s} must hold {d}..{d} entries", .{ try label(ctx, path), @as(u64, @intFromFloat(min.?)), @as(u64, @intFromFloat(max.?)) });
        return ctx.fail("more than {d} entries in {s}", .{ @as(u64, @intFromFloat(max.?)), try label(ctx, path) });
    }
    if (min != null and len < min.?) {
        if (window) return ctx.fail("{s} must hold {d}..{d} entries", .{ try label(ctx, path), @as(u64, @intFromFloat(min.?)), @as(u64, @intFromFloat(max.?)) });
        return ctx.fail("{s} must hold at least {d} entries", .{ try label(ctx, path), @as(u64, @intFromFloat(min.?)) });
    }
    if (getObj(spec, "items")) |items| {
        for (v.array.items, 0..) |x, i| try checkValue(ctx, x, items, try item(ctx, path, i), null);
    }
}

pub fn checkObject(ctx: Ctx, v: Value, spec: ObjectMap, path: Path) Error!void {
    if (v != .object) return ctx.fail("{s} must be an object", .{try label(ctx, path)});
    if (spec.get("fields") != null or spec.get("minFields") != null)
        try checkFields(ctx, v.object, getObj(spec, "fields") orelse .empty, spec, path, &.{});
}

pub fn checkValue(ctx: Ctx, v: Value, spec: ObjectMap, path: Path, parent: ?ObjectMap) Error!void {
    const t = getStr(spec, "type") orelse "";
    if (std.mem.eql(u8, t, "string")) return checkString(ctx, v, spec, path, parent);
    if (std.mem.eql(u8, t, "integer") or std.mem.eql(u8, t, "number")) return checkNumber(ctx, v, spec, path);
    if (std.mem.eql(u8, t, "boolean")) return checkBoolean(ctx, v, spec, path);
    if (std.mem.eql(u8, t, "array")) return checkArray(ctx, v, spec, path);
    if (std.mem.eql(u8, t, "object")) return checkObject(ctx, v, spec, path);
    std.debug.panic("opRegistry: unknown type \"{s}\"", .{t});
}

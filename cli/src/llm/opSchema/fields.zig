//! The cross-field layer: the native `rules` fold, the presence rules an object's
//! holder declares (forms / together / exclusive / minFields / onlyWith / requiredWith),
//! and the normalizing pick that keeps only the declared keys.
const std = @import("std");
const json = @import("json.zig");
const ctxm = @import("ctx.zig");
const checks = @import("checks.zig");

const Value = json.Value;
const ObjectMap = json.ObjectMap;
const Error = json.Error;
const Ctx = ctxm.Ctx;
const Path = ctxm.Path;
const where = ctxm.where;
const label = ctxm.label;
const child = ctxm.child;
const numText = ctxm.numText;
const quoteList = ctxm.quoteList;
const quoteKeys = ctxm.quoteKeys;
const present = json.present;
const getArr = json.getArr;
const getObj = json.getObj;
const getStr = json.getStr;
const getBool = json.getBool;
const numOf = json.numOf;
const valueEq = json.valueEq;
const inList = json.inList;
const trimWs = json.trimWs;
const checkValue = checks.checkValue;

// native cross-field rules an entry may name in `rules`

/// §3.2 tolerance: "aspect" beside "spec" folds into the spec when it lacks one; a
/// conflicting duplicate fails. The folded copy is what gets validated + normalized.
pub fn cropAspectFold(ctx: Ctx, a: ObjectMap) Error!ObjectMap {
    const beside = a.get("aspect") orelse return a;
    if (beside == .null) return a;
    const spec_v = a.get("spec") orelse return a;
    if (spec_v != .object) return a;
    var spec: ObjectMap = .empty;
    var it = spec_v.object.iterator();
    while (it.next()) |kv| try spec.put(ctx.a, kv.key_ptr.*, kv.value_ptr.*);
    if (present(spec, "aspect")) {
        if (!valueEq(spec.get("aspect").?, beside)) return ctx.fail("\"aspect\" appears both beside \"spec\" and inside it with different values", .{});
    } else try spec.put(ctx.a, "aspect", beside);
    var out: ObjectMap = .empty;
    var ait = a.iterator();
    while (ait.next()) |kv| {
        if (std.mem.eql(u8, kv.key_ptr.*, "aspect")) continue;
        try out.put(ctx.a, kv.key_ptr.*, if (std.mem.eql(u8, kv.key_ptr.*, "spec")) Value{ .object = spec } else kv.value_ptr.*);
    }
    return out;
}

pub fn checkFields(ctx: Ctx, obj: ObjectMap, fields: ObjectMap, holder: ObjectMap, path: ?Path, skip: []const []const u8) Error!void {
    // `allowUnknown` (the envelope's variant objects) tolerates undeclared keys.
    outer: for (obj.keys()) |k| {
        if (getBool(holder, "allowUnknown")) break;
        for (skip) |s| {
            if (std.mem.eql(u8, k, s)) continue :outer;
        }
        if (fields.get(k) == null) {
            if (path) |p| return ctx.fail("unknown field \"{s}\" in {s}", .{ k, try where(ctx, p) });
            return ctx.fail("unknown field \"{s}\"", .{k});
        }
    }
    if (getArr(holder, "forms")) |forms| {
        // The present keys among those the forms mention must equal exactly one group.
        var given: std.ArrayList([]const u8) = .empty;
        for (fields.keys()) |k| {
            var mentioned = false;
            for (forms) |f| for (f.array.items) |fk| {
                if (std.mem.eql(u8, fk.string, k)) mentioned = true;
            };
            if (mentioned and present(obj, k)) try given.append(ctx.a, k);
        }
        var matched: usize = 0;
        for (forms) |f| {
            if (f.array.items.len != given.items.len) continue;
            var all = true;
            for (f.array.items) |fk| {
                var found = false;
                for (given.items) |g| found = found or std.mem.eql(u8, fk.string, g);
                all = all and found;
            }
            if (all) matched += 1;
        }
        if (matched != 1) {
            var wording: std.ArrayList(u8) = .empty;
            for (forms, 0..) |f, i| {
                if (i != 0) try wording.appendSlice(ctx.a, " / ");
                try wording.appendSlice(ctx.a, try quoteKeys(ctx, f.array.items, "+"));
            }
            return ctx.fail("exactly one of {s} is required", .{wording.items});
        }
    }
    if (getArr(holder, "together")) |groups| for (groups) |g| {
        var n: usize = 0;
        for (g.array.items) |k| {
            if (present(obj, k.string)) n += 1;
        }
        if (n != 0 and n != g.array.items.len) return ctx.fail("{s} ride together", .{try quoteKeys(ctx, g.array.items, " and ")});
    };
    if (getArr(holder, "exclusive")) |groups| for (groups) |g| {
        var n: usize = 0;
        for (g.array.items) |k| {
            if (present(obj, k.string)) n += 1;
        }
        if (n > 1) return ctx.fail("carries both {s} — at most one of them", .{try quoteKeys(ctx, g.array.items, " and ")});
    };
    if (holder.get("minFields")) |m| {
        const min = numOf(m).?;
        var n: f64 = 0;
        for (fields.keys()) |k| {
            if (present(obj, k)) n += 1;
        }
        if (n < min) {
            var names: std.ArrayList(u8) = .empty;
            for (fields.keys(), 0..) |k, i| {
                if (i != 0) try names.append(ctx.a, '/');
                try names.appendSlice(ctx.a, k);
            }
            if (min == 1) return ctx.fail("needs at least one of {s}", .{names.items});
            return ctx.fail("needs at least {s} of {s}", .{ try numText(ctx, m), names.items });
        }
    }
    var it = fields.iterator();
    while (it.next()) |kv| {
        const k = kv.key_ptr.*;
        const spec = kv.value_ptr.object;
        const at = try child(ctx, path, k);
        if (!present(obj, k)) {
            if (getBool(spec, "required")) return ctx.fail("{s} is required", .{try label(ctx, at)});
            if (getObj(spec, "requiredWith")) |deps| {
                var dit = deps.iterator();
                while (dit.next()) |d| {
                    const dep_v = obj.get(d.key_ptr.*) orelse continue;
                    if (inList(dep_v, d.value_ptr.*)) {
                        var one = [_]Value{dep_v};
                        return ctx.fail("{s} is required with \"{s}\" {s}", .{ try label(ctx, at), d.key_ptr.*, try quoteList(ctx, .{ .array = std.json.Array.fromOwnedSlice(ctx.a, &one) }) });
                    }
                }
            }
            continue;
        }
        if (getObj(spec, "onlyWith")) |deps| {
            var dit = deps.iterator();
            while (dit.next()) |d| {
                const dep_v = obj.get(d.key_ptr.*) orelse Value.null;
                if (!inList(dep_v, d.value_ptr.*))
                    return ctx.fail("{s} only applies with \"{s}\" {s}", .{ try label(ctx, at), d.key_ptr.*, try quoteKeys(ctx, d.value_ptr.array.items, " or ") });
            }
        }
        try checkValue(ctx, obj.get(k).?, spec, at, obj);
    }
}

// normalization: the declared keys only, defaults applied, trims honoured

pub fn pick(a: std.mem.Allocator, v: Value, spec: ObjectMap) Error!Value {
    const t = getStr(spec, "type") orelse "";
    if (std.mem.eql(u8, t, "object") and v == .object) {
        if (getObj(spec, "fields")) |fields| return .{ .object = try pickFields(a, v.object, fields) };
    }
    if (std.mem.eql(u8, t, "array") and v == .array) {
        var out = std.json.Array.init(a);
        if (getObj(spec, "items")) |items| {
            for (v.array.items) |x| try out.append(try pick(a, x, items));
        } else try out.appendSlice(v.array.items);
        return .{ .array = out };
    }
    if (std.mem.eql(u8, t, "string") and getBool(spec, "trim") and v == .string) return .{ .string = trimWs(v.string) };
    return v;
}

pub fn pickFields(a: std.mem.Allocator, obj: ObjectMap, fields: ObjectMap) Error!ObjectMap {
    var out: ObjectMap = .empty;
    var it = fields.iterator();
    while (it.next()) |kv| {
        const k = kv.key_ptr.*;
        if (present(obj, k)) {
            try out.put(a, k, try pick(a, obj.get(k).?, kv.value_ptr.object));
        } else if (kv.value_ptr.object.get("default")) |d| try out.put(a, k, d);
    }
    return out;
}

//! std.json value helpers shared by the schema package: JS-shaped number reading
//! (an overflowed literal arrives as `number_string`), typed getters and SameValueZero.
const std = @import("std");

pub const Value = std.json.Value;
pub const ObjectMap = std.json.ObjectMap;
pub const Error = error{ Invalid, OutOfMemory };

/// A finite number: integers, finite floats, and the overflowed literals std.json
/// keeps as text (JSON.parse reads those as finite floats too).
pub fn numOf(v: Value) ?f64 {
    return switch (v) {
        .integer => |i| @floatFromInt(i),
        .float => |f| if (std.math.isFinite(f)) f else null,
        .number_string => |s| blk: {
            const f = std.fmt.parseFloat(f64, s) catch break :blk null;
            break :blk if (std.math.isFinite(f)) f else null;
        },
        else => null,
    };
}

pub fn isInt(v: Value) bool {
    const f = numOf(v) orelse return false;
    return @floor(f) == f;
}

pub fn present(obj: ObjectMap, key: []const u8) bool {
    const v = obj.get(key) orelse return false;
    return v != .null;
}

pub fn getObj(o: ObjectMap, key: []const u8) ?ObjectMap {
    const v = o.get(key) orelse return null;
    return if (v == .object) v.object else null;
}

pub fn getArr(o: ObjectMap, key: []const u8) ?[]Value {
    const v = o.get(key) orelse return null;
    return if (v == .array) v.array.items else null;
}

pub fn getStr(o: ObjectMap, key: []const u8) ?[]const u8 {
    const v = o.get(key) orelse return null;
    return if (v == .string) v.string else null;
}

pub fn getBool(o: ObjectMap, key: []const u8) bool {
    const v = o.get(key) orelse return false;
    return v == .bool and v.bool;
}

pub fn strEq(v: Value, s: []const u8) bool {
    return v == .string and std.mem.eql(u8, v.string, s);
}

/// JS SameValueZero over the JSON primitives.
pub fn valueEq(x: Value, y: Value) bool {
    switch (x) {
        .string => |s| return strEq(y, s),
        .bool => |b| return y == .bool and y.bool == b,
        .integer, .float, .number_string => {
            const fx = numOf(x) orelse return false;
            const fy = numOf(y) orelse return false;
            return fx == fy;
        },
        else => return false,
    }
}

pub fn inList(v: Value, list: Value) bool {
    if (list != .array) return false;
    for (list.array.items) |x| {
        if (valueEq(v, x)) return true;
    }
    return false;
}

pub fn trimWs(s: []const u8) []const u8 {
    return std.mem.trim(u8, s, &std.ascii.whitespace);
}

const testing = std.testing;

test "numbers: 3.0 is an integer, overflowed literals are finite numbers, NaN-ish text is not" {
    try testing.expect(isInt(.{ .float = 3.0 }) and !isInt(.{ .float = 1.5 }));
    try testing.expect(numOf(.{ .number_string = "123456789012345678901234567890" }) != null);
    try testing.expect(numOf(.{ .number_string = "1e999" }) == null);
    try testing.expect(numOf(.{ .string = "3" }) == null);
}

//! The two std.json member lookups the wire layer and the transport share.
const std = @import("std");

/// The named member of a JSON object value, or null (also when `v` isn't an object).
pub fn member(v: std.json.Value, key: []const u8) ?std.json.Value {
    if (v != .object) return null;
    return v.object.get(key);
}

/// The named member when it is a string, or null.
pub fn memberStr(v: std.json.Value, key: []const u8) ?[]const u8 {
    const m = member(v, key) orelse return null;
    return if (m == .string) m.string else null;
}

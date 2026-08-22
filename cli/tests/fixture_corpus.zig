//! Shared plumbing for the cross-surface fixture-corpus walkers
//! (tests/*_fixtures_test.zig). The corpus lives in the browser tree;
//! `zig build test` runs with cwd = cli/ (verified empirically), so fixtures
//! resolve via "../browser/js/config/...". std.fs + std.json only.
const std = @import("std");

pub const corpus_root = "../browser/js/config/";

/// Local walker overrides for measured cli-vs-corpus disagreements, keyed
/// family → fixture/case name. The shared fixtures are NEVER edited; this file
/// pins where the cli's current behavior diverges from the corpus expectation.
pub const overrides_json = @embedFile("fixture_overrides.json");

pub fn parseOverrides(a: std.mem.Allocator) !std.json.Value {
    return std.json.parseFromSliceLeaky(std.json.Value, a, overrides_json, .{});
}

pub fn overrideFor(overrides: std.json.Value, family: []const u8, name: []const u8) ?std.json.Value {
    const fam = member(overrides, family) orelse return null;
    return member(fam, name);
}

pub fn readAlloc(a: std.mem.Allocator, io: std.Io, sub: []const u8) ![]u8 {
    var buf: [512]u8 = undefined;
    const path = try std.fmt.bufPrint(&buf, "{s}{s}", .{ corpus_root, sub });
    return std.Io.Dir.cwd().readFileAlloc(io, path, a, .limited(16 * 1024 * 1024));
}

pub fn loadJson(a: std.mem.Allocator, io: std.Io, sub: []const u8) !std.json.Value {
    const bytes = try readAlloc(a, io, sub);
    return std.json.parseFromSliceLeaky(std.json.Value, a, bytes, .{}) catch
        // One corpus file deliberately stores a LONE surrogate escape (the browser's
        // UTF-16 length-cap divergence pin); std.json rejects it, so neutralize
        // unpaired surrogates to U+FFFD and reparse. The affected expectation belongs
        // to a DIVERGENCE case the cli walker recomputes locally anyway.
        std.json.parseFromSliceLeaky(std.json.Value, a, try fixLoneSurrogates(a, bytes), .{});
}

/// Rewrite `\uD800..\uDBFF` escapes not followed by a low surrogate (and lone low
/// surrogates) as `�` so std.json accepts the document.
fn fixLoneSurrogates(a: std.mem.Allocator, bytes: []const u8) ![]u8 {
    var out: std.ArrayList(u8) = .empty;
    var i: usize = 0;
    while (i < bytes.len) {
        const c = bytes[i];
        if (c != '\\') {
            try out.append(a, c);
            i += 1;
            continue;
        }
        if (surrogateAt(bytes, i)) |code| {
            const high = code < 0xDC00;
            const paired = high and surrogateAt(bytes, i + 6) != null and surrogateAt(bytes, i + 6).? >= 0xDC00;
            if (paired) {
                try out.appendSlice(a, bytes[i .. i + 12]);
                i += 12;
            } else {
                try out.appendSlice(a, "\\uFFFD");
                i += 6;
            }
            continue;
        }
        // Any other escape: copy the backslash + the escaped char verbatim.
        try out.append(a, c);
        i += 1;
        if (i < bytes.len) {
            try out.append(a, bytes[i]);
            i += 1;
        }
    }
    return out.toOwnedSlice(a);
}

/// The surrogate code unit of a `\uD800..\uDFFF` escape starting at `i`, or null.
fn surrogateAt(bytes: []const u8, i: usize) ?u16 {
    if (i + 6 > bytes.len or bytes[i] != '\\' or bytes[i + 1] != 'u') return null;
    const code = std.fmt.parseInt(u16, bytes[i + 2 .. i + 6], 16) catch return null;
    return if (code >= 0xD800 and code <= 0xDFFF) code else null;
}

/// The sorted *.json basenames of a corpus directory (all arena-owned).
pub fn listJson(a: std.mem.Allocator, io: std.Io, sub: []const u8) ![][]const u8 {
    var buf: [512]u8 = undefined;
    const path = try std.fmt.bufPrint(&buf, "{s}{s}", .{ corpus_root, sub });
    var dir = try std.Io.Dir.cwd().openDir(io, path, .{ .iterate = true });
    defer dir.close(io);
    var names: std.ArrayList([]const u8) = .empty;
    var it = dir.iterate();
    while (try it.next(io)) |entry| {
        if (entry.kind != .file or !std.mem.endsWith(u8, entry.name, ".json")) continue;
        try names.append(a, try a.dupe(u8, entry.name));
    }
    const slice = try names.toOwnedSlice(a);
    std.mem.sort([]const u8, slice, {}, strLess);
    return slice;
}

fn strLess(_: void, x: []const u8, y: []const u8) bool {
    return std.mem.order(u8, x, y) == .lt;
}

pub fn member(v: std.json.Value, key: []const u8) ?std.json.Value {
    if (v != .object) return null;
    return v.object.get(key);
}

pub fn memberStr(v: std.json.Value, key: []const u8) ?[]const u8 {
    const m = member(v, key) orelse return null;
    return if (m == .string) m.string else null;
}

fn numOf(v: std.json.Value) ?f64 {
    return switch (v) {
        .integer => |i| @floatFromInt(i),
        .float => |f| f,
        .number_string => |s| std.fmt.parseFloat(f64, s) catch null,
        else => null,
    };
}

/// Structural JSON equality: objects by key set (order-free), numbers by value.
pub fn jsonEquals(x: std.json.Value, y: std.json.Value) bool {
    switch (x) {
        .null => return y == .null,
        .bool => |b| return y == .bool and y.bool == b,
        .integer, .float, .number_string => {
            const fx = numOf(x) orelse return false;
            const fy = numOf(y) orelse return false;
            return fx == fy;
        },
        .string => |s| return y == .string and std.mem.eql(u8, s, y.string),
        .array => |arr| {
            if (y != .array or y.array.items.len != arr.items.len) return false;
            for (arr.items, y.array.items) |ai, bi| {
                if (!jsonEquals(ai, bi)) return false;
            }
            return true;
        },
        .object => |obj| {
            if (y != .object or y.object.count() != obj.count()) return false;
            var it = obj.iterator();
            while (it.next()) |kv| {
                const yv = y.object.get(kv.key_ptr.*) orelse return false;
                if (!jsonEquals(kv.value_ptr.*, yv)) return false;
            }
            return true;
        },
    }
}

pub fn stringify(a: std.mem.Allocator, v: std.json.Value) ![]u8 {
    return std.json.Stringify.valueAlloc(a, v, .{});
}

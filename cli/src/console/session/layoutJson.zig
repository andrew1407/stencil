//! The lines/layout JSON the session round-trips: pulling the lines array out of a layout
//! document, merging two, reading a layout back into an `EditState`, and the typed member
//! lookups those need.
const std = @import("std");
const image = @import("../../media/image.zig");
const core = @import("../../core.zig");
const layout_mod = @import("../../media/layout.zig");
const EditState = @import("../session.zig").EditState;

pub fn rasterizeLinesJson(gpa: std.mem.Allocator, img: *image.Rgba8, lines_json: []const u8) void {
    const wrapped = std.fmt.allocPrint(gpa, "{{\"lines\":{s}}}", .{lines_json}) catch return;
    defer gpa.free(wrapped);
    var parsed = layout_mod.parse(gpa, wrapped) catch return;
    defer parsed.deinit();
    for (parsed.lines) |line| {
        core.rasterizeLine(img.pixels, @intCast(img.width), @intCast(img.height), line);
    }
}

/// Extract the `lines` array of a layout JSON document as an owned JSON array string ("[]" if
/// absent). Caller owns the result.
pub fn extractLinesJson(gpa: std.mem.Allocator, layout_bytes: []const u8) ![]u8 {
    var parsed = std.json.parseFromSlice(std.json.Value, gpa, layout_bytes, .{}) catch return gpa.dupe(u8, "[]");
    defer parsed.deinit();
    if (parsed.value == .object) {
        if (parsed.value.object.get("lines")) |lv| {
            if (lv == .array) return std.json.Stringify.valueAlloc(gpa, lv, .{});
        }
    }
    return gpa.dupe(u8, "[]");
}

/// Concatenate two JSON array strings ("[...]") into one. Pure string work. Caller owns it.
pub fn mergeLinesJson(gpa: std.mem.Allocator, a: []const u8, b: []const u8) ![]u8 {
    const ai = innerArray(a);
    const bi = innerArray(b);
    if (ai.len == 0) return gpa.dupe(u8, if (bi.len == 0) "[]" else b);
    if (bi.len == 0) return gpa.dupe(u8, a);
    return std.fmt.allocPrint(gpa, "[{s},{s}]", .{ ai, bi });
}

/// The contents between the outermost `[` `]` of a JSON array string, trimmed (empty if none).
pub fn innerArray(s: []const u8) []const u8 {
    const t = std.mem.trim(u8, s, " \t\r\n");
    if (t.len < 2 or t[0] != '[' or t[t.len - 1] != ']') return "";
    return std.mem.trim(u8, t[1 .. t.len - 1], " \t\r\n");
}

/// Read a server layout document into an EditState (rotation, crop, filter, lines).
pub fn parseLayoutInto(gpa: std.mem.Allocator, layout_bytes: []const u8, out: *EditState) !void {
    out.lines_json = try extractLinesJson(gpa, layout_bytes);
    var parsed = std.json.parseFromSlice(std.json.Value, gpa, layout_bytes, .{}) catch return;
    defer parsed.deinit();
    if (parsed.value != .object) return;
    const obj = parsed.value.object;
    if (jsonStr(obj, "imageFilter")) |m| out.filter_mode = try gpa.dupe(u8, m);
    if (jsonStr(obj, "filterColor")) |c| out.filter_color = try gpa.dupe(u8, c);
    if (jsonInt(obj, "rotationQuarters")) |r| out.rotation = core.normalizeQuarters(@intCast(r));
    if (obj.get("cropRect")) |cv| {
        if (cv == .object) {
            const co = cv.object;
            out.crop = .{
                .x = @intFromFloat(jsonNum(co, "x")),
                .y = @intFromFloat(jsonNum(co, "y")),
                .w = @intFromFloat(jsonNum(co, "width")),
                .h = @intFromFloat(jsonNum(co, "height")),
            };
        }
    }
}

pub fn jsonStr(obj: std.json.ObjectMap, key: []const u8) ?[]const u8 {
    if (obj.get(key)) |v| {
        if (v == .string) return v.string;
    }
    return null;
}

pub fn jsonInt(obj: std.json.ObjectMap, key: []const u8) ?i64 {
    if (obj.get(key)) |v| {
        return switch (v) {
            .integer => |i| i,
            .float => |f| @intFromFloat(f),
            else => null,
        };
    }
    return null;
}

pub fn jsonNum(obj: std.json.ObjectMap, key: []const u8) f64 {
    if (obj.get(key)) |v| {
        return switch (v) {
            .integer => |i| @floatFromInt(i),
            .float => |f| f,
            else => 0,
        };
    }
    return 0;
}

const testing = std.testing;

test "mergeLinesJson concatenates arrays, handles empties" {
    const a = testing.allocator;
    const m1 = try mergeLinesJson(a, "[{\"a\":1}]", "[{\"b\":2}]");
    defer a.free(m1);
    try testing.expectEqualStrings("[{\"a\":1},{\"b\":2}]", m1);
    const m2 = try mergeLinesJson(a, "[]", "[{\"b\":2}]");
    defer a.free(m2);
    try testing.expectEqualStrings("[{\"b\":2}]", m2);
    const m3 = try mergeLinesJson(a, "[{\"a\":1}]", "[]");
    defer a.free(m3);
    try testing.expectEqualStrings("[{\"a\":1}]", m3);
}
test "extractLinesJson pulls the lines array, defaults to []" {
    const a = testing.allocator;
    const l = try extractLinesJson(a, "{\"lines\":[{\"color\":\"#f00\"}],\"imageFilter\":\"bw\"}");
    defer a.free(l);
    try testing.expectEqualStrings("[{\"color\":\"#f00\"}]", l);
    const none = try extractLinesJson(a, "{\"imageFilter\":\"bw\"}");
    defer a.free(none);
    try testing.expectEqualStrings("[]", none);
}

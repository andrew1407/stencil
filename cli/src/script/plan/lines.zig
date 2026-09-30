//! The lines an executor shows after each planned `layout`, as the planner keeps them: a plan's
//! `layout` REPLACES the drawn lines (llm-contract §2), so each one carries the whole set. They
//! follow a crop by core.cropChange as `--script`'s marks do, and a layout document's lines are
//! read by the loader `--script` reads them with.
const std = @import("std");

const core = @import("../../core.zig");
const layout_mod = @import("../../media/layout.zig");
const net = @import("../../net.zig");
const pipeline = @import("../../pipeline.zig");

/// `limits.MAX_LAYOUT_LINES` of common/config/llm/opRegistry.json: the most one layout holds.
pub const MAX_LINES: usize = 200;

pub const Lines = []const core.LineDraw;

/// A shape's points copied out of the resolve buffer the next decode reuses.
pub fn owned(a: std.mem.Allocator, line: core.LineDraw) !core.LineDraw {
    var copy = line;
    copy.points = try a.dupe(f64, line.points);
    return copy;
}

/// `under` with `over` drawn on top of it, as one new list.
pub fn joined(a: std.mem.Allocator, under: Lines, over: Lines) !Lines {
    const out = try a.alloc(core.LineDraw, under.len + over.len);
    @memcpy(out[0..under.len], under);
    @memcpy(out[under.len..], over);
    return out;
}

/// `set` after a crop moved the window from `old` to `new`, as `--script`'s Marks.recrop moves it.
pub fn recropped(a: std.mem.Allocator, set: Lines, old: core.Rect, new: core.Rect) !Lines {
    const change = core.cropChange(old, new);
    if (change.orientation_changed) return &.{};
    if (change.scale == 1) return set;
    const out = try a.alloc(core.LineDraw, set.len);
    for (set, out) |line, *moved| {
        moved.* = line;
        const pts = try a.dupe(f64, line.points);
        for (pts) |*v| v.* *= change.scale;
        moved.points = pts;
    }
    return out;
}

/// A layout document's lines, or why they did not load: read, or fetched through the guard
/// `--script` fetches it with (`strict` refusing loopback too), then parsed as `--script` does.
pub const Doc = union(enum) { lines: Lines, failed: []const u8 };

pub fn document(a: std.mem.Allocator, io: std.Io, src: []const u8, is_url: bool, strict: bool) Doc {
    const bytes = (if (is_url) net.fetch(a, io, src, strict) else pipeline.loadLayoutBytes(a, io, src)) catch
        return .{ .failed = if (is_url) "the fetch failed" else "the file cannot be read" };
    const doc = layout_mod.parse(a, bytes) catch return .{ .failed = "it is not layout JSON" };
    return .{ .lines = doc.lines };
}

const testing = std.testing;

test "a crop scales the lines by the width ratio, and an album/portrait flip clears them" {
    var arena: std.heap.ArenaAllocator = .init(testing.allocator);
    defer arena.deinit();
    var pts = [_]f64{ 2, 4, 8, 6 };
    const set = [_]core.LineDraw{.{ .points = &pts, .color = "red", .thickness = 2, .point_size = 0, .style = "solid", .locked = false, .fill_color = "" }};
    const half = try recropped(arena.allocator(), &set, .{ .x = 0, .y = 0, .w = 16, .h = 12 }, .{ .x = 4, .y = 3, .w = 8, .h = 6 });
    try testing.expectEqualSlices(f64, &.{ 1, 2, 4, 3 }, half[0].points);
    try testing.expectEqual(@as(f64, 2), pts[0]); // the set it was given is left alone
    const flipped = try recropped(arena.allocator(), &set, .{ .x = 0, .y = 0, .w = 16, .h = 12 }, .{ .x = 0, .y = 0, .w = 6, .h = 12 });
    try testing.expectEqual(@as(usize, 0), flipped.len);
}

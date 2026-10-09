//! The session's state records: one undoable `EditState` snapshot (mirror + rotation + crop +
//! filter + lines — the browser-compatible model) and one `Attachment` the turn will send.
const std = @import("std");
const image = @import("../../media/image.zig");
const core = @import("../../core.zig");

pub const max_states = 64; // pristine + up to 63 undoable edits; older edits drop off the front

/// One editing snapshot mirroring the browser layout: a left-right mirror then a rotation (0..3
/// clockwise quarters) of the original, a crop rect in rotated-original pixels, a filter, and the lines as JSON.
pub const EditState = struct {
    rotation: i32 = 0,
    mirrored: bool = false,
    crop: ?core.Rect = null,
    filter_mode: []u8 = &.{},
    filter_color: []u8 = &.{},
    lines_json: []u8 = &.{},
    points: usize = 0, // the points `lines_json` holds, counted as the state is kept (pointsIn)

    pub fn deinit(self: *EditState, gpa: std.mem.Allocator) void {
        if (self.filter_mode.len != 0) gpa.free(self.filter_mode);
        if (self.filter_color.len != 0) gpa.free(self.filter_color);
        if (self.lines_json.len != 0) gpa.free(self.lines_json);
        self.* = .{};
    }

    pub fn dupe(self: EditState, gpa: std.mem.Allocator) !EditState {
        var out = EditState{ .rotation = self.rotation, .mirrored = self.mirrored, .crop = self.crop, .points = self.points };
        errdefer out.deinit(gpa);
        out.filter_mode = try gpa.dupe(u8, self.filter_mode);
        out.filter_color = try gpa.dupe(u8, self.filter_color);
        out.lines_json = try gpa.dupe(u8, self.lines_json);
        return out;
    }

    pub fn lines(self: EditState) []const u8 {
        return if (self.lines_json.len == 0) "[]" else self.lines_json;
    }
};

/// How many points a lines JSON array holds (0 when it is no array), for the history's points cap.
pub fn pointsIn(gpa: std.mem.Allocator, lines_json: []const u8) usize {
    var arena = std.heap.ArenaAllocator.init(gpa);
    defer arena.deinit();
    const v = std.json.parseFromSliceLeaky(std.json.Value, arena.allocator(), lines_json, .{}) catch return 0;
    if (v != .array) return 0;
    var n: usize = 0;
    for (v.array.items) |l| if (l == .object) if (l.object.get("points")) |p| if (p == .array) {
        n += p.array.items.len;
    };
    return n;
}

/// How many edits after the pristine [0] drop off the front, oldest first: while more than
/// `max_steps` states stand or, the newest aside, their points exceed `max_points` (core HistoryStack).
pub fn evictions(states: []const EditState, max_steps: usize, max_points: usize) usize {
    var total: usize = 0;
    for (states) |st| total += st.points;
    var drop: usize = 0;
    while (states.len - drop > max_steps or (states.len - drop > 2 and total > max_points)) : (drop += 1) {
        total -= states[1 + drop].points;
    }
    return drop;
}

/// One image brought into the turn with `/upload` (§2.1/§7): its label plus the raw ENCODED bytes —
/// kept instead of pixels, so a turn costs kilobytes and a `save` embeds the untouched original.
pub const Attachment = struct {
    label: []u8,
    bytes: []u8,
    fmt: image.Format = .png,
    temp: bool = false, // came from a URL/in-memory source, not a file on disk

    pub fn deinit(self: *Attachment, gpa: std.mem.Allocator) void {
        gpa.free(self.label);
        gpa.free(self.bytes);
        self.* = undefined;
    }
};

const testing = std.testing;

test "pointsIn counts every line's points; anything else holds none" {
    try testing.expectEqual(@as(usize, 3), pointsIn(testing.allocator, "[{\"points\":[{\"x\":1,\"y\":1},{\"x\":2,\"y\":2}]},{\"points\":[{\"x\":3,\"y\":3}]},7]"));
    for ([_][]const u8{ "[]", "", "{}", "[{\"points\":1}]" }) |none| try testing.expectEqual(@as(usize, 0), pointsIn(testing.allocator, none));
}

test "evictions: the depth first, then the points, never the pristine state or the newest" {
    const of = struct {
        fn states(comptime pts: []const usize) [pts.len]EditState {
            var out: [pts.len]EditState = undefined;
            for (pts, &out) |n, *st| st.* = .{ .points = n };
            return out;
        }
    }.states;
    try testing.expectEqual(@as(usize, 0), evictions(&of(&.{ 0, 3, 3 }), 4, 6));
    try testing.expectEqual(@as(usize, 1), evictions(&of(&.{ 0, 3, 3, 1 }), 3, 100));
    try testing.expectEqual(@as(usize, 1), evictions(&of(&.{ 0, 3, 3, 3 }), 64, 6));
    try testing.expectEqual(@as(usize, 2), evictions(&of(&.{ 0, 3, 3, 9 }), 64, 6)); // the newest alone stays
    try testing.expectEqual(@as(usize, 2), evictions(&of(&.{ 4, 1, 1, 1 }), 64, 5)); // the pristine points count
}

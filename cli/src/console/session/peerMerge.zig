//! The co-edit merge a 409 conflict retries with, twin of the browser's core/remote/push.js
//! mergePeer and the desktop's model/lineUnion: the peer's lines first, then each local line
//! core::mergeKeep keeps, and the peer's filter unless ours changed since the last push.
//! `/apply combine` stays the plain concatenation of combineLinesJson. Both keep the join whole;
//! every draw cuts it at the layout caps (media/layout/lines.zig), as the GUIs cut theirs.
const std = @import("std");
const merge = @import("../../media/layout/merge.zig");
const layoutJson = @import("layoutJson.zig");
const Session = @import("../session.zig").Session;

/// A layout's filter as the browser's adoptServerFilter reads it: `imageFilter`, else the
/// legacy `blackAndWhite` flag; the tint only when the layout names one.
pub const PeerFilter = struct { mode: []const u8, color: ?[]const u8 };

pub fn peerFilter(obj: std.json.ObjectMap) PeerFilter {
    const mode = layoutJson.jsonStr(obj, "imageFilter") orelse "";
    const bw = if (obj.get("blackAndWhite")) |v| v == .bool and v.bool else false;
    const color = layoutJson.jsonStr(obj, "filterColor") orelse "";
    return .{ .mode = if (mode.len != 0) mode else if (bw) "bw" else "none", .color = if (color.len != 0) color else null };
}

fn modeOf(mode: []const u8) []const u8 {
    return if (mode.len == 0) "none" else mode;
}

fn replace(gpa: std.mem.Allocator, slot: *[]u8, value: []const u8) !void {
    const dup = try gpa.dupe(u8, value);
    if (slot.len != 0) gpa.free(slot.*);
    slot.* = dup;
}

/// A peer saved first: its lines join ours and, while our filter is unchanged since the last
/// push, its filter replaces ours — one history state when either changed anything.
pub fn mergePeer(self: *Session, layout_json: []const u8) !void {
    var arena = std.heap.ArenaAllocator.init(self.gpa);
    defer arena.deinit();
    const a = arena.allocator();
    const cur = self.state();
    const merged = try merge.unionLinesJson(a, try layoutJson.extractLinesJson(a, layout_json), cur.lines());
    var mode: ?[]const u8 = null;
    var color: ?[]const u8 = null;
    const doc = std.json.parseFromSliceLeaky(std.json.Value, a, layout_json, .{}) catch .null;
    if (!self.filter_dirty and doc == .object) {
        const f = peerFilter(doc.object);
        if (!std.mem.eql(u8, f.mode, modeOf(cur.filter_mode))) mode = f.mode;
        if (f.color) |c| {
            if (!std.mem.eql(u8, c, cur.filter_color)) color = c;
        }
    }
    if (!merged.peer_added and mode == null and color == null) return;
    var next = try cur.dupe(self.gpa);
    errdefer next.deinit(self.gpa);
    if (merged.peer_added) try replace(self.gpa, &next.lines_json, merged.json);
    if (mode) |m| try replace(self.gpa, &next.filter_mode, m);
    if (color) |c| try replace(self.gpa, &next.filter_color, c);
    try self.pushState(next);
}

const testing = std.testing;

test "peerFilter reads imageFilter, else blackAndWhite, and a tint only when named" {
    const a = testing.allocator;
    const cases = [_]struct { json: []const u8, mode: []const u8, color: ?[]const u8 }{
        .{ .json = "{\"imageFilter\":\"custom\",\"filterColor\":\"#7c3aed\"}", .mode = "custom", .color = "#7c3aed" },
        .{ .json = "{\"imageFilter\":\"\",\"blackAndWhite\":true}", .mode = "bw", .color = null },
        .{ .json = "{\"filter\":\"sepia\",\"filterColor\":\"\"}", .mode = "none", .color = null },
    };
    for (cases) |c| {
        const doc = try std.json.parseFromSlice(std.json.Value, a, c.json, .{});
        defer doc.deinit();
        const f = peerFilter(doc.value.object);
        try testing.expectEqualStrings(c.mode, f.mode);
        if (c.color) |want| try testing.expectEqualStrings(want, f.color.?) else try testing.expect(f.color == null);
    }
}

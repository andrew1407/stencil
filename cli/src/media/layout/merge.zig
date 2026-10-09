//! The co-edit line union, twin of the browser's core/remote/push.js mergePeer and the desktop's
//! model/lineUnion: the peer's lines first, then each local line core::mergeKeep keeps. The
//! console's 409 retry and `--merge-lines` (CONTRACT.md §8) both join here; every draw cuts the
//! join at the layout caps (lines.zig), as the GUIs cut theirs.
const std = @import("std");
const core = @import("../../core.zig");
const layout = @import("lines.zig");

/// The joined JSON array (caller-owned), and whether a peer line matches none of ours.
pub const LineUnion = struct { json: []u8, peer_added: bool };

fn arrayOf(a: std.mem.Allocator, json: []const u8) []std.json.Value {
    const v = std.json.parseFromSliceLeaky(std.json.Value, a, json, .{}) catch return &.{};
    return if (v == .array) v.array.items else &.{};
}

fn drawsOf(a: std.mem.Allocator, items: []const std.json.Value) ![]core.LineDraw {
    const out = try a.alloc(core.LineDraw, items.len);
    for (items, out) |v, *d| d.* = try layout.lineOf(a, if (v == .object) v.object else .empty, std.math.maxInt(usize));
    return out;
}

pub fn unionLinesJson(gpa: std.mem.Allocator, peer_json: []const u8, local_json: []const u8) !LineUnion {
    var arena = std.heap.ArenaAllocator.init(gpa);
    defer arena.deinit();
    const a = arena.allocator();
    const peer = arrayOf(a, peer_json);
    const local = arrayOf(a, local_json);
    const peer_draws = try drawsOf(a, peer);
    const local_draws = try drawsOf(a, local);
    const keep = try core.mergeKeep(a, peer_draws, local_draws);
    const fresh = try core.mergeKeep(a, local_draws, peer_draws);
    var out = std.json.Array.init(a);
    try out.appendSlice(peer);
    for (local, keep) |v, k| if (k) try out.append(v);
    return .{
        .json = try std.json.Stringify.valueAlloc(gpa, std.json.Value{ .array = out }, .{}),
        .peer_added = std.mem.indexOfScalar(bool, fresh, true) != null,
    };
}

/// The local lines no `seen` line keys the same (caller-owned JSON array): a line an earlier pass
/// adopted is the peer's, so the peer's later delete or move of it is not undone (push.js peerKeys).
pub fn dropSeenJson(gpa: std.mem.Allocator, seen_json: []const u8, local_json: []const u8) ![]u8 {
    var arena = std.heap.ArenaAllocator.init(gpa);
    defer arena.deinit();
    const a = arena.allocator();
    const local = arrayOf(a, local_json);
    const keep = try core.mergeKeep(a, try drawsOf(a, arrayOf(a, seen_json)), try drawsOf(a, local));
    var out = std.json.Array.init(a);
    for (local, keep) |v, k| if (k) try out.append(v);
    return std.json.Stringify.valueAlloc(gpa, std.json.Value{ .array = out }, .{});
}

const testing = std.testing;

test "unionLinesJson: the peer's lines first, then ours that none of theirs key the same" {
    const a = testing.allocator;
    const peer = "[{\"points\":[{\"x\":1,\"y\":2}],\"color\":\"#f00\"}]";
    const mine = "[{\"points\":[{\"x\":1,\"y\":2}],\"color\":\"#f00\",\"thickness\":2},{\"points\":[{\"x\":3,\"y\":4}]}]";
    const u = try unionLinesJson(a, peer, mine);
    defer a.free(u.json);
    try testing.expectEqualStrings("[{\"points\":[{\"x\":1,\"y\":2}],\"color\":\"#f00\"},{\"points\":[{\"x\":3,\"y\":4}]}]", u.json);
    try testing.expect(!u.peer_added);

    const theirs = try unionLinesJson(a, "[{\"points\":[{\"x\":9,\"y\":9}]}]", "[]");
    defer a.free(theirs.json);
    try testing.expectEqualStrings("[{\"points\":[{\"x\":9,\"y\":9}]}]", theirs.json);
    try testing.expect(theirs.peer_added);
}

test "dropSeenJson: a local line keyed like a seen one is the peer's, and goes" {
    const a = testing.allocator;
    const seen = "[{\"points\":[{\"x\":1,\"y\":1}]}]";
    const local = "[{\"points\":[{\"x\":1,\"y\":1}]},{\"points\":[{\"x\":2,\"y\":2}]}]";
    const kept = try dropSeenJson(a, seen, local);
    defer a.free(kept);
    try testing.expectEqualStrings("[{\"points\":[{\"x\":2,\"y\":2}]}]", kept);
    const all = try dropSeenJson(a, "[]", local);
    defer a.free(all);
    try testing.expectEqualStrings(local, all);

    // The peer deleted the seen line: the union no longer brings it back.
    const u = try unionLinesJson(a, "[]", kept);
    defer a.free(u.json);
    try testing.expectEqualStrings("[{\"points\":[{\"x\":2,\"y\":2}]}]", u.json);
    try testing.expect(!u.peer_added);
}

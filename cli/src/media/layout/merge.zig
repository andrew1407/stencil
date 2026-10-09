//! The co-edit line union, twin of the browser's core/remote/push.js mergePeer and the desktop's
//! model/lineUnion: the local lines a `seen` line keys drop, then the peer's lines first and each
//! local line core::mergeKeep keeps. The console's 409 retry (seen = the last pass's peer lines)
//! and `--merge-lines` (CONTRACT.md §8) both join here; every draw cuts the join at the layout
//! caps (lines.zig), as the GUIs cut theirs.
const std = @import("std");
const core = @import("../../core.zig");
const layout = @import("lines.zig");

/// The joined lines, whether a peer line matches none of ours, and whether a seen line took one.
pub const Joined = struct { lines: []std.json.Value, peer_added: bool, dropped: bool };

/// The JSON form of `Joined`, the array caller-owned.
pub const LineUnion = struct { json: []u8, peer_added: bool, dropped: bool };

fn arrayOf(a: std.mem.Allocator, json: []const u8) []std.json.Value {
    const v = std.json.parseFromSliceLeaky(std.json.Value, a, json, .{}) catch return &.{};
    return if (v == .array) v.array.items else &.{};
}

fn drawsOf(a: std.mem.Allocator, items: []const std.json.Value) ![]core.LineDraw {
    const out = try a.alloc(core.LineDraw, items.len);
    for (items, out) |v, *d| d.* = try layout.lineOf(a, if (v == .object) v.object else .empty, std.math.maxInt(usize));
    return out;
}

/// A local line keyed like a `seen` one is the peer's from an earlier pass, so the peer's later
/// delete or move of it is not undone (push.js peerKeys): it drops before the union. In `a`.
pub fn join(a: std.mem.Allocator, peer: []const std.json.Value, local: []const std.json.Value, seen: []const std.json.Value) !Joined {
    const unseen = try core.mergeKeep(a, try drawsOf(a, seen), try drawsOf(a, local));
    var mine: std.ArrayList(std.json.Value) = try .initCapacity(a, local.len);
    for (local, unseen) |v, k| if (k) mine.appendAssumeCapacity(v);
    const peer_draws = try drawsOf(a, peer);
    const local_draws = try drawsOf(a, mine.items);
    const keep = try core.mergeKeep(a, peer_draws, local_draws);
    const fresh = try core.mergeKeep(a, local_draws, peer_draws);
    var out: std.ArrayList(std.json.Value) = try .initCapacity(a, peer.len + mine.items.len);
    out.appendSliceAssumeCapacity(peer);
    for (mine.items, keep) |v, k| if (k) out.appendAssumeCapacity(v);
    return .{
        .lines = out.items,
        .peer_added = std.mem.indexOfScalar(bool, fresh, true) != null,
        .dropped = mine.items.len != local.len,
    };
}

/// `join` over JSON arrays (an unparsable one reads as empty), for the session's string state.
pub fn joinJson(gpa: std.mem.Allocator, peer_json: []const u8, local_json: []const u8, seen_json: []const u8) !LineUnion {
    var arena = std.heap.ArenaAllocator.init(gpa);
    defer arena.deinit();
    const a = arena.allocator();
    const j = try join(a, arrayOf(a, peer_json), arrayOf(a, local_json), arrayOf(a, seen_json));
    const arr = std.json.Array.fromOwnedSlice(a, j.lines);
    return .{
        .json = try std.json.Stringify.valueAlloc(gpa, std.json.Value{ .array = arr }, .{}),
        .peer_added = j.peer_added,
        .dropped = j.dropped,
    };
}

const testing = std.testing;

test "joinJson: the peer's lines first, then ours that none of theirs key the same" {
    const a = testing.allocator;
    const peer = "[{\"points\":[{\"x\":1,\"y\":2}],\"color\":\"#f00\"}]";
    const mine = "[{\"points\":[{\"x\":1,\"y\":2}],\"color\":\"#f00\",\"thickness\":2},{\"points\":[{\"x\":3,\"y\":4}]}]";
    const u = try joinJson(a, peer, mine, "[]");
    defer a.free(u.json);
    try testing.expectEqualStrings("[{\"points\":[{\"x\":1,\"y\":2}],\"color\":\"#f00\"},{\"points\":[{\"x\":3,\"y\":4}]}]", u.json);
    try testing.expect(!u.peer_added and !u.dropped);

    const theirs = try joinJson(a, "[{\"points\":[{\"x\":9,\"y\":9}]}]", "[]", "");
    defer a.free(theirs.json);
    try testing.expectEqualStrings("[{\"points\":[{\"x\":9,\"y\":9}]}]", theirs.json);
    try testing.expect(theirs.peer_added);
}

test "joinJson: a local line keyed like a seen one is the peer's, so their delete holds" {
    const a = testing.allocator;
    const seen = "[{\"points\":[{\"x\":1,\"y\":1}]}]";
    const local = "[{\"points\":[{\"x\":1,\"y\":1}]},{\"points\":[{\"x\":2,\"y\":2}]}]";
    const u = try joinJson(a, "[]", local, seen);
    defer a.free(u.json);
    try testing.expectEqualStrings("[{\"points\":[{\"x\":2,\"y\":2}]}]", u.json);
    try testing.expect(!u.peer_added and u.dropped);
    const all = try joinJson(a, "[]", local, "[]");
    defer a.free(all.json);
    try testing.expectEqualStrings(local, all.json);
    try testing.expect(!all.dropped);
}

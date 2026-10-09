// `--merge-lines` as an adapter drives it: the envelope on stdout, the seen filter ahead of the
// union, and the refusals main.zig turns into exit 2 (CONTRACT.md §8).
const std = @import("std");
const mergeLines = @import("../../src/inspect/mergeLines.zig");
const input = @import("../../src/safety/input.zig");
const testing = std.testing;

fn merged(raw: []const u8) ![]u8 {
    var out: std.Io.Writer.Allocating = .init(testing.allocator);
    errdefer out.deinit();
    try mergeLines.writeMerge(testing.allocator, &out.writer, raw);
    return out.toOwnedSlice();
}

test "merge-lines: one envelope, the peer's lines first, then ours none of theirs key" {
    const doc = try merged("{\"peer\":[{\"points\":[{\"x\":1,\"y\":1}]}],\"local\":[{\"points\":[{\"x\":1,\"y\":1}]},{\"points\":[{\"x\":2,\"y\":2}]}]}");
    defer testing.allocator.free(doc);
    try testing.expectEqualStrings("{\"version\":1,\"lines\":[{\"points\":[{\"x\":1,\"y\":1}]},{\"points\":[{\"x\":2,\"y\":2}]}],\"peerAdded\":false}\n", doc);

    const added = try merged("{\"peer\":[{\"points\":[{\"x\":9,\"y\":9}]}],\"local\":[]}");
    defer testing.allocator.free(added);
    try testing.expectEqualStrings("{\"version\":1,\"lines\":[{\"points\":[{\"x\":9,\"y\":9}]}],\"peerAdded\":true}\n", added);
}

test "merge-lines: a local line the last pass saw is the peer's, so their delete holds" {
    const doc = try merged("{\"peer\":[],\"local\":[{\"points\":[{\"x\":1,\"y\":1}]},{\"points\":[{\"x\":2,\"y\":2}]}],\"seen\":[{\"points\":[{\"x\":1,\"y\":1}]}]}");
    defer testing.allocator.free(doc);
    try testing.expectEqualStrings("{\"version\":1,\"lines\":[{\"points\":[{\"x\":2,\"y\":2}]}],\"peerAdded\":false}\n", doc);
}

test "merge-lines: an input that is no {peer, local} object is a usage error" {
    const bad = [_][]const u8{
        "[]",                                    "null",          "{\"peer\":[",
        "{\"local\":[]}",                        "{\"peer\":[]}", "{\"peer\":{},\"local\":[]}",
        "{\"peer\":[],\"local\":[],\"seen\":1}",
    };
    for (bad) |raw| try testing.expectError(mergeLines.Error.Usage, merged(raw));
}

test "merge-lines: an input that cannot be read is refused before anything prints" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    var out: std.Io.Writer.Allocating = .init(testing.allocator);
    defer out.deinit();
    try testing.expectError(input.Error.Unreadable, mergeLines.run(testing.allocator, threaded.io(), &out.writer, "../merge.json"));
    try testing.expectError(input.Error.Unreadable, mergeLines.run(testing.allocator, threaded.io(), &out.writer, "stencil_no_merge.json"));
    try testing.expectEqual(@as(usize, 0), out.written().len);
}

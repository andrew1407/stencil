//! `--merge-lines`: a peer's line list and ours joined as a 409 retry joins them (media/layout/
//! merge.zig), printed as one JSON document, so an adapter (the bot) merges without a port of
//! core's line keys (CONTRACT.md §8). Reads the file, or stdin for "-".
const std = @import("std");
const input = @import("../safety/input.zig");
const merge = @import("../media/layout/merge.zig");
const report = @import("../app/report.zig");

/// This envelope's version, bumped only when a consumer must change.
pub const VERSION = 1;

/// The input is not `{"peer":[…],"local":[…]}` (`seen` optional, an array): the caller exits 2.
pub const Error = error{Usage};

fn arrayField(obj: std.json.ObjectMap, key: []const u8, required: bool) ![]const std.json.Value {
    const v = obj.get(key) orelse {
        if (!required) return &.{};
        report.err("the merge input has no \"{s}\" array\n", .{key});
        return Error.Usage;
    };
    if (v != .array) {
        report.err("the merge input's \"{s}\" is not an array\n", .{key});
        return Error.Usage;
    }
    return v.array.items;
}

/// Write the envelope for the merge input `raw`: parsed once, serialized once.
pub fn writeMerge(gpa: std.mem.Allocator, out: *std.Io.Writer, raw: []const u8) !void {
    var arena: std.heap.ArenaAllocator = .init(gpa);
    defer arena.deinit();
    const a = arena.allocator();
    const root = std.json.parseFromSliceLeaky(std.json.Value, a, raw, .{}) catch .null;
    if (root != .object) {
        report.err("the merge input is not a JSON object {{\"peer\":[…],\"local\":[…],\"seen\":[…]}}\n", .{});
        return Error.Usage;
    }
    const peer = try arrayField(root.object, "peer", true);
    const local = try arrayField(root.object, "local", true);
    const seen = try arrayField(root.object, "seen", false);
    const joined = try merge.join(a, peer, local, seen);
    var js: std.json.Stringify = .{ .writer = out };
    try js.beginObject();
    try js.objectField("version");
    try js.write(VERSION);
    try js.objectField("lines");
    try js.beginArray();
    for (joined.lines) |v| try js.write(v);
    try js.endArray();
    try js.objectField("peerAdded");
    try js.write(joined.peer_added);
    try js.endObject();
    try out.writeByte('\n');
}

pub fn run(gpa: std.mem.Allocator, io: std.Io, out: *std.Io.Writer, path: []const u8) !void {
    const raw = try input.read(gpa, io, path, "merge input");
    defer gpa.free(raw);
    try writeMerge(gpa, out, raw);
    try out.flush();
}

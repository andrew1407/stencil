//! `--plan-check`: one model reply walked by core under a surface's schema and printed as one JSON
//! document — core's result plus the registry that judged it — so an adapter validates a plan
//! without a validator of its own (CONTRACT.md §7). Reads the file, or stdin for "-".
const std = @import("std");
const opplan = @import("../core/opplan.zig");
const confine = @import("../safety/confine.zig");
const report = @import("../app/report.zig");

/// This envelope's version, bumped only when a consumer must change.
pub const VERSION = 1;
pub const MAX_REPLY_BYTES: usize = 8 << 20;

/// A reply that cannot be read, or a schema core refuses: the caller exits 2.
pub const Error = error{Usage};

fn readReply(gpa: std.mem.Allocator, io: std.Io, path: []const u8) (Error || error{OutOfMemory})![]u8 {
    if (std.mem.eql(u8, path, "-")) {
        var buf: [4096]u8 = undefined;
        var stdin = std.Io.File.stdin().readerStreaming(io, &buf);
        return stdin.interface.allocRemaining(gpa, .limited(MAX_REPLY_BYTES)) catch |e| return readFailed("<stdin>", e);
    }
    if (confine.hasParentTraversal(path)) {
        report.err("refusing to read a reply that climbs out of the working directory: '{s}'\n", .{path});
        return Error.Usage;
    }
    return std.Io.Dir.cwd().readFileAlloc(io, path, gpa, .limited(MAX_REPLY_BYTES)) catch |e| readFailed(path, e);
}

fn readFailed(label: []const u8, e: anyerror) (Error || error{OutOfMemory}) {
    if (e == error.OutOfMemory) return error.OutOfMemory;
    if (e == error.StreamTooLong) {
        report.err("that reply is too large: {s} (the cap is {d} bytes)\n", .{ label, MAX_REPLY_BYTES });
    } else {
        report.err("cannot read the reply {s} ({s})\n", .{ label, @errorName(e) });
    }
    return Error.Usage;
}

/// Write the envelope for `raw` walked under `surface`; returns core's verdict.
pub fn writeCheck(
    gpa: std.mem.Allocator,
    out: *std.Io.Writer,
    raw: []const u8,
    surface: []const u8,
    capabilities: ?[]const u8,
) !opplan.Status {
    var arena: std.heap.ArenaAllocator = .init(gpa);
    defer arena.deinit();
    const a = arena.allocator();
    const schema = opplan.Schema.open(try a.dupeZ(u8, surface), if (capabilities) |cap| try a.dupeZ(u8, cap) else null);
    defer schema.close();
    if (schema.failure().len != 0) {
        report.err("core refused the op registry for '{s}': {s}\n", .{ surface, schema.failure() });
        return Error.Usage;
    }
    const entries = (try std.json.parseFromSliceLeaky(std.json.Value, a, schema.entries(), .{})).object;
    const walked = try schema.walk(a, raw);
    var js: std.json.Stringify = .{ .writer = out };
    try js.beginObject();
    try js.objectField("version");
    try js.write(VERSION);
    try js.objectField("surface");
    try js.write(surface);
    try js.objectField("registryBytes");
    try js.write(entries.get("registryBytes").?);
    try js.objectField("registryFnv1a64");
    try js.write(entries.get("registryFnv1a64").?);
    try js.objectField("result");
    try js.beginWriteRaw();
    try out.writeAll(walked.json);
    js.endWriteRaw();
    try js.endObject();
    try out.writeByte('\n');
    return walked.status;
}

pub fn run(
    gpa: std.mem.Allocator,
    io: std.Io,
    out: *std.Io.Writer,
    path: []const u8,
    surface: []const u8,
    capabilities: ?[]const u8,
) !opplan.Status {
    const raw = try readReply(gpa, io, path);
    defer gpa.free(raw);
    const status = try writeCheck(gpa, out, raw, surface, capabilities);
    try out.flush();
    return status;
}

const testing = std.testing;

fn checked(raw: []const u8, surface: []const u8, capabilities: ?[]const u8) !struct { status: opplan.Status, doc: std.json.Parsed(std.json.Value) } {
    var out: std.Io.Writer.Allocating = .init(testing.allocator);
    defer out.deinit();
    const status = try writeCheck(testing.allocator, &out.writer, raw, surface, capabilities);
    try testing.expect(std.mem.endsWith(u8, out.written(), "}\n") and std.mem.count(u8, out.written(), "\n") == 1);
    return .{ .status = status, .doc = try std.json.parseFromSlice(std.json.Value, testing.allocator, out.written(), .{}) };
}

test "plan check: one envelope — version, surface, the registry's size and hash, core's result" {
    var r = try checked("{\"reply\":\"ok\",\"actions\":[{\"op\":\"rotate\",\"dir\":\"left\"}]}", "mcp", null);
    defer r.doc.deinit();
    try testing.expectEqual(opplan.Status.valid, r.status);
    const root = r.doc.value.object;
    try testing.expectEqual(@as(i64, VERSION), root.get("version").?.integer);
    try testing.expectEqualStrings("mcp", root.get("surface").?.string);
    try testing.expectEqual(@as(i64, @intCast(opplan.registry_json.len)), root.get("registryBytes").?.integer);
    try testing.expectEqual(@as(usize, 16), root.get("registryFnv1a64").?.string.len);
    const result = root.get("result").?.object;
    try testing.expectEqualStrings("valid", result.get("status").?.string);
    try testing.expectEqual(@as(i64, 1), result.get("actions").?.array.items[0].object.get("times").?.integer);
}

test "plan check: the verdict follows the surface and its capabilities" {
    var forbidden = try checked("{\"reply\":\"x\",\"actions\":[{\"op\":\"llm\"}]}", "cli", null);
    defer forbidden.doc.deinit();
    try testing.expectEqual(opplan.Status.invalid, forbidden.status);
    try testing.expectEqualStrings("E_FORBIDDEN", forbidden.doc.value.object.get("result").?.object.get("error").?.object.get("code").?.string);
    var skipped = try checked("{\"reply\":\"x\",\"actions\":[{\"op\":\"llm\"}]}", "bot", null);
    defer skipped.doc.deinit();
    try testing.expectEqual(opplan.Status.valid, skipped.status);
    var gated = try checked("{\"reply\":\"x\",\"actions\":[{\"op\":\"save\"}]}", "cli", "loadAttachment");
    defer gated.doc.deinit();
    try testing.expect(std.mem.indexOf(u8, gated.doc.value.object.get("result").?.object.get("warnings").?.array.items[0].object.get("code").?.string, "W_UNKNOWN_OP") != null);
    var chat = try checked("just chatting", "cli", null);
    defer chat.doc.deinit();
    try testing.expectEqual(opplan.Status.chat_only, chat.status);
}

//! `--script` against its own `--script-plan` replayed through the console's op-plan executor,
//! `runPlan`, one plan at a time as an adapter feeds them: the pixels each leaves for the same
//! ops under the same `@source` head. The replay suites compare the two.
const std = @import("std");
const testing = std.testing;

const console = @import("../../src/console.zig");
const logo = @import("../../src/app/logo.zig");
const plan = @import("../../src/script/plan.zig");
const runPlan = @import("../../src/console/llm/plan.zig").runPlan;
const run = @import("../../src/script/run.zig");
const scriptCore = @import("../../src/script/core.zig");
const pixelsOf = @import("pixels.zig").pixelsOf;

const stc = "stencil_replay.stc";
const out = "stencil_replay.png";

fn quiet(_: *anyopaque, _: []const u8) void {}

/// What `--script` saves for `ops` under `head`.
fn oneShot(io: std.Io, head: []const u8, ops: []const u8) ![]u8 {
    const dir = std.Io.Dir.cwd();
    const text = try std.fmt.allocPrint(testing.allocator, "{s}{s}; @save " ++ out ++ "\n", .{ head, ops });
    defer testing.allocator.free(text);
    try dir.writeFile(io, .{ .sub_path = stc, .data = text });
    defer dir.deleteFile(io, stc) catch {};
    defer dir.deleteFile(io, out) catch {};
    try run.run(testing.allocator, io, .{}, stc);
    return pixelsOf(testing.allocator, io, out);
}

/// What the console's executor shows after every plan `--script-plan` makes of `ops`, the
/// script itself standing in for the user text its openFile/openUrl targets must echo.
fn replayed(io: std.Io, head: []const u8, ops: []const u8) ![]u8 {
    const gpa = testing.allocator;
    const text = try std.fmt.allocPrint(gpa, "{s}{s}\n", .{ head, ops });
    defer gpa.free(text);
    var s = try scriptCore.Script.parse(text);
    defer s.deinit();
    var env: std.Io.Writer.Allocating = .init(gpa);
    defer env.deinit();
    try plan.writeEnvelope(gpa, io, &env.writer, s, .{}, "r.stc");
    var parsed = try std.json.parseFromSlice(std.json.Value, gpa, env.written(), .{});
    defer parsed.deinit();

    var unused: u8 = 0;
    logo.setSink(quiet, &unused);
    defer logo.clearSink();
    var session: console.Session = .{ .gpa = gpa };
    defer session.deinit();
    const plans = parsed.value.object.get("blocks").?.array.items[0].object.get("plans").?.array.items;
    for (plans) |p| {
        const raw = try std.json.Stringify.valueAlloc(gpa, p, .{});
        defer gpa.free(raw);
        _ = try runPlan(&session, io, raw, text);
    }
    return gpa.dupe(u8, session.current().pixels);
}

pub fn expectSame(io: std.Io, head: []const u8, ops: []const u8) !void {
    const want = try oneShot(io, head, ops);
    defer testing.allocator.free(want);
    const got = try replayed(io, head, ops);
    defer testing.allocator.free(got);
    try testing.expectEqualSlices(u8, want, got);
}

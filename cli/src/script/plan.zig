//! `--script-plan <file>`: the script lowered to op-plan JSON on STDOUT, for the adapters
//! that drive an editor rather than the pixels (mcp, bot). Nothing is written. The only reads:
//! a header probe of each local input, the guarded fetch sizing a URL input whose block draws
//! lines, and the layout documents a `@layout` names. The writer comes in from main.zig.
const std = @import("std");

const args = @import("../args.zig");
const scriptCore = @import("core.zig");

const load = @import("load.zig");
const diagnostics = @import("plan/diagnostics.zig");
const plan_block = @import("plan/block.zig");
const sequence = @import("plan/sequence.zig");
const opplan = @import("../core/opplan.zig");

/// Envelope version. Bumped only when a consumer has to change; see cli/CONTRACT.md §4.3.
pub const VERSION: i64 = 1;

/// The registry's `limits.MAX_ACTIONS`, a plan's size; tests/script/script_plan_test.zig pins it.
pub const MAX_ACTIONS = sequence.MAX_ACTIONS;

/// The whole envelope, and whether every block planned. A script with an error still reports
/// its diagnostics but lowers to no blocks — nothing in it is safe to act on — and so does one
/// the planner refuses, its reason a diagnostic among the rest.
pub fn envelope(
    gpa: std.mem.Allocator,
    io: std.Io,
    out: *std.Io.Writer,
    script: scriptCore.Script,
    opts: args.Options,
    label: []const u8,
) !bool {
    var arena: std.heap.ArenaAllocator = .init(gpa);
    defer arena.deinit();
    const a = arena.allocator();

    var blocks: std.Io.Writer.Allocating = .init(a);
    var refused: ?scriptCore.Diagnostic = null;
    if (!script.hasErrors()) {
        const checker: ?opplan.Schema = if (opts.plan_surface) |surface| opplan.Schema.open(
            try a.dupeZ(u8, surface),
            if (opts.plan_capabilities) |cap| try a.dupeZ(u8, cap) else null,
        ) else null;
        defer if (checker) |c| c.close();
        // The bot is url-only (llm-contract §10) and its scripts come from chat users.
        const remote = if (opts.plan_surface) |surface| std.mem.eql(u8, surface, "bot") else false;
        var env: sequence.Env = .{ .io = io, .remote = remote };
        var bjs: std.json.Stringify = .{ .writer = &blocks.writer };
        try bjs.beginArray();
        var b: u32 = 0;
        while (b < script.blockCount()) : (b += 1) plan_block.writeBlock(a, &env, &bjs, script, opts, b, checker) catch |e| switch (e) {
            error.PlanRefused => {
                refused = diagnostics.ofRefusal(script, env.refusal.?);
                break;
            },
            else => return e,
        };
        if (refused == null) try bjs.endArray(); // a refusal leaves the half-written block unread
    }

    var js: std.json.Stringify = .{ .writer = out };
    try js.beginObject();
    try js.objectField("version");
    try js.write(VERSION);
    try js.objectField("script");
    try js.write(label);
    try js.objectField("diagnostics");
    try diagnostics.write(&js, script, refused);
    try js.objectField("blocks");
    try js.beginWriteRaw();
    try out.writeAll(if (script.hasErrors() or refused != null) "[]" else blocks.written());
    js.endWriteRaw();
    try js.endObject();
    try out.writeByte('\n');
    return !script.hasErrors() and refused == null;
}

pub fn writeEnvelope(
    gpa: std.mem.Allocator,
    io: std.Io,
    out: *std.Io.Writer,
    script: scriptCore.Script,
    opts: args.Options,
    label: []const u8,
) !void {
    _ = try envelope(gpa, io, out, script, opts, label);
}

pub fn run(gpa: std.mem.Allocator, io: std.Io, out: *std.Io.Writer, opts: args.Options, path: []const u8) !void {
    const source = try load.readScript(gpa, io, path);
    defer gpa.free(source);

    var script = scriptCore.Script.parse(source) catch return load.Error.ScriptUnreadable;
    defer script.deinit();

    const planned = try envelope(gpa, io, out, script, opts, load.labelFor(path));
    try out.flush();
    if (!planned) return load.Error.ScriptHasErrors;
}

test {
    _ = diagnostics;
    _ = plan_block;
}

const testing = std.testing;

test "the envelope carries the version, the label and one block per @source" {
    const gpa = testing.allocator;
    var threaded = std.Io.Threaded.init(gpa, .{});
    defer threaded.deinit();

    var s = try scriptCore.Script.parse("@source a.png:\n  @filter bw\n  @save\n");
    defer s.deinit();

    var out: std.Io.Writer.Allocating = .init(gpa);
    defer out.deinit();
    try writeEnvelope(gpa, threaded.io(), &out.writer, s, .{}, "a.stc");

    var parsed = try std.json.parseFromSlice(std.json.Value, gpa, out.written(), .{});
    defer parsed.deinit();
    const root = parsed.value.object;
    try testing.expectEqual(@as(i64, VERSION), root.get("version").?.integer);
    try testing.expectEqualStrings("a.stc", root.get("script").?.string);
    try testing.expectEqual(@as(usize, 0), root.get("diagnostics").?.array.items.len);

    const block = root.get("blocks").?.array.items[0].object;
    try testing.expectEqualStrings("file", block.get("sourceKind").?.string);
    try testing.expectEqualStrings("a.png", block.get("inputs").?.array.items[0].string);
    try testing.expectEqualStrings("a-stencil.png", block.get("saves").?.array.items[0].object.get("path").?.string);
}

test "a script with an error reports its diagnostics and lowers to no blocks" {
    const gpa = testing.allocator;
    var threaded = std.Io.Threaded.init(gpa, .{});
    defer threaded.deinit();

    var s = try scriptCore.Script.parse("@source a.png:\n  @crp 10%\n");
    defer s.deinit();

    var out: std.Io.Writer.Allocating = .init(gpa);
    defer out.deinit();
    try writeEnvelope(gpa, threaded.io(), &out.writer, s, .{}, "a.stc");

    var parsed = try std.json.parseFromSlice(std.json.Value, gpa, out.written(), .{});
    defer parsed.deinit();
    try testing.expectEqual(@as(usize, 0), parsed.value.object.get("blocks").?.array.items.len);
    const diag = parsed.value.object.get("diagnostics").?.array.items[0].object;
    try testing.expectEqualStrings("error", diag.get("severity").?.string);
    try testing.expectEqualStrings("E_UNKNOWN_DIRECTIVE", diag.get("code").?.string);
}

test "each block carries its op stream with every length as written, for any input to resolve" {
    const gpa = testing.allocator;
    var threaded = std.Io.Threaded.init(gpa, .{});
    defer threaded.deinit();

    var s = try scriptCore.Script.parse("@source clip.mp4:\n  @rect (10%, 5px) (-10%, -1in)\n  @save\n");
    defer s.deinit();
    var out: std.Io.Writer.Allocating = .init(gpa);
    defer out.deinit();
    try writeEnvelope(gpa, threaded.io(), &out.writer, s, .{}, "a.stc");

    var parsed = try std.json.parseFromSlice(std.json.Value, gpa, out.written(), .{});
    defer parsed.deinit();
    const block = parsed.value.object.get("blocks").?.array.items[0].object;
    const ops = block.get("ops").?.array.items;
    try testing.expectEqualStrings("rect", ops[1].object.get("kind").?.string);
    try testing.expectEqualStrings("10%", ops[1].object.get("toks").?.array.items[0].string);
    try testing.expectEqualStrings("-1in", ops[1].object.get("toks").?.array.items[3].string);
    // A video is never probed, so the pixel plan drops the shape the token stream still holds.
    try testing.expectEqual(@as(usize, 2), block.get("plans").?.array.items[0].object.get("actions").?.array.items.len);
}

test "with --plan-surface each chunk carries that surface's core verdict" {
    const gpa = testing.allocator;
    var threaded = std.Io.Threaded.init(gpa, .{});
    defer threaded.deinit();

    var s = try scriptCore.Script.parse("@source https://e.example/a.png:\n  @filter bw\n  @save\n");
    defer s.deinit();
    var out: std.Io.Writer.Allocating = .init(gpa);
    defer out.deinit();
    try writeEnvelope(gpa, threaded.io(), &out.writer, s, .{ .plan_surface = "bot" }, "a.stc");

    var parsed = try std.json.parseFromSlice(std.json.Value, gpa, out.written(), .{});
    defer parsed.deinit();
    const plan = parsed.value.object.get("blocks").?.array.items[0].object.get("plans").?.array.items[0].object;
    const check = plan.get("check").?.object;
    try testing.expectEqualStrings("valid", check.get("status").?.string);
    try testing.expectEqual(plan.get("actions").?.array.items.len, check.get("actions").?.array.items.len);
    // Without the flag the envelope keeps its §4.3 shape.
    var plain: std.Io.Writer.Allocating = .init(gpa);
    defer plain.deinit();
    try writeEnvelope(gpa, threaded.io(), &plain.writer, s, .{}, "a.stc");
    try testing.expect(std.mem.indexOf(u8, plain.written(), "\"check\"") == null);
}

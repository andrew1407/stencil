//! The --script-plan envelope: the z-order a run paints, a JSON round trip naming every save,
//! actions the op-plan validator accepts, nothing planned for a script with an error, the
//! registry's own action cap, and each input of a glob planned against its own size.
const std = @import("std");
const testing = std.testing;

const opSchema = @import("../../src/llm/opSchema.zig");
const opplan = @import("../../src/llm/opplan.zig");
const image = @import("../../src/media/image.zig");
const plan = @import("../../src/script/plan.zig");
const run = @import("../../src/script/run.zig");
const scriptCore = @import("../../src/script/core.zig");
const pixelsOf = @import("pixels.zig").pixelsOf;

/// The index of the first action whose `op` is `op_name` and, when `path` is given, whose
/// "path" is that file — the plan's input load and a `@layout` load share an op name.
fn actionAt(actions: []const std.json.Value, op_name: []const u8, path: ?[]const u8) ?usize {
    for (actions, 0..) |v, i| {
        if (!std.mem.eql(u8, v.object.get("op").?.string, op_name)) continue;
        const want = path orelse return i;
        const got = v.object.get("path") orelse continue;
        if (std.mem.eql(u8, got.string, want)) return i;
    }
    return null;
}

test "--script-plan advertises the z-order --script paints: a @layout lands on the marks before it" {
    const gpa = testing.allocator;
    var threaded = std.Io.Threaded.init(gpa, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();

    const doc = "stencil_script_zorder.json";
    const stc = "stencil_script_zorder.stc";
    const out = "stencil_script_zorder.png";
    const text = "@source tests/fixtures/sample.png:\n    @use line #ff0000 5px\n" ++
        "    @line (0,6) (15,6)\n    @layout " ++ doc ++ "\n    @save " ++ out ++ "\n";

    try dir.writeFile(io, .{ .sub_path = doc, .data = "{\"imageWidth\":16,\"imageHeight\":12,\"lines\":[" ++
        "{\"points\":[{\"x\":0,\"y\":6},{\"x\":15,\"y\":6}],\"color\":\"#00ff00\",\"thickness\":5," ++
        "\"pointSize\":0,\"style\":\"solid\",\"fillColor\":\"transparent\"}]}" });
    try dir.writeFile(io, .{ .sub_path = stc, .data = text });
    defer dir.deleteFile(io, doc) catch {};
    defer dir.deleteFile(io, stc) catch {};
    defer dir.deleteFile(io, out) catch {};

    // What the pixels do: the layout's green covers the red @line under it.
    try run.run(gpa, io, .{}, stc);
    const px = try pixelsOf(gpa, io, out);
    defer gpa.free(px);
    const centre = (6 * 16 + 8) * 4;
    try testing.expect(px[centre + 1] > px[centre]);

    var s = try scriptCore.Script.parse(text);
    defer s.deinit();
    var env: std.Io.Writer.Allocating = .init(gpa);
    defer env.deinit();
    try plan.writeEnvelope(gpa, io, &env.writer, s, .{}, "z.stc");

    var parsed = try std.json.parseFromSlice(std.json.Value, gpa, env.written(), .{});
    defer parsed.deinit();
    const actions = parsed.value.object.get("blocks").?.array.items[0]
        .object.get("plans").?.array.items[0].object.get("actions").?.array.items;
    // What the plan says: the marks land, then one layout sets them with the doc's lines on top.
    try testing.expectEqual(@as(usize, 4), actions.len);
    const both = actions[2].object.get("lines").?.array.items;
    try testing.expectEqualStrings("#ff0000", both[0].object.get("color").?.string);
    try testing.expectEqualStrings("#00ff00", both[1].object.get("color").?.string);
    try testing.expectEqualStrings("save", actions[3].object.get("op").?.string);
}

test "the plan envelope round-trips through std.json and names every save" {
    const gpa = testing.allocator;
    var threaded = std.Io.Threaded.init(gpa, .{});
    defer threaded.deinit();

    var s = try scriptCore.Script.parse(
        "@source tests/fixtures/sample.png:\n  @crop x1=10% x2=-10%\n  @filter bw\n  @save out/\n",
    );
    defer s.deinit();

    var out: std.Io.Writer.Allocating = .init(gpa);
    defer out.deinit();
    try plan.writeEnvelope(gpa, threaded.io(), &out.writer, s, .{}, "s.stc");

    var parsed = try std.json.parseFromSlice(std.json.Value, gpa, out.written(), .{});
    defer parsed.deinit();
    const root = parsed.value.object;
    try testing.expectEqual(@as(i64, plan.VERSION), root.get("version").?.integer);

    const block = root.get("blocks").?.array.items[0].object;
    try testing.expectEqual(@as(i64, 16), block.get("dims").?.object.get("width").?.integer);
    const actions = block.get("plans").?.array.items[0].object.get("actions").?.array.items;
    try testing.expectEqual(@as(usize, 4), actions.len);
    try testing.expectEqualStrings("openFile", actions[0].object.get("op").?.string);
    try testing.expectEqualStrings("-10%", actions[1].object.get("spec").?.object.get("x2").?.string);
    try testing.expectEqualStrings("bw", actions[2].object.get("mode").?.string);

    const saves = block.get("saves").?.array.items;
    try testing.expectEqual(@as(usize, 1), saves.len);
    try testing.expectEqualStrings("out/sample-stencil.png", saves[0].object.get("path").?.string);
}

test "a plan's actions are a plan the op-plan validator itself accepts" {
    const gpa = testing.allocator;
    var threaded = std.Io.Threaded.init(gpa, .{});
    defer threaded.deinit();

    var s = try scriptCore.Script.parse(
        "@source tests/fixtures/sample.png:\n  @crop 10%\n  @rect (1,1) (4,4)\n  @filter aqua\n  @save o.png\n",
    );
    defer s.deinit();

    var out: std.Io.Writer.Allocating = .init(gpa);
    defer out.deinit();
    try plan.writeEnvelope(gpa, threaded.io(), &out.writer, s, .{}, "s.stc");

    var parsed = try std.json.parseFromSlice(std.json.Value, gpa, out.written(), .{});
    defer parsed.deinit();
    const one = parsed.value.object.get("blocks").?.array.items[0]
        .object.get("plans").?.array.items[0];
    const raw = try std.json.Stringify.valueAlloc(gpa, one, .{});
    defer gpa.free(raw);

    switch (try opplan.parsePlan(gpa, raw)) {
        .plan => |p| {
            var validated = p;
            defer validated.deinit();
            try testing.expectEqual(@as(usize, 5), validated.actions.len);
            try testing.expectEqualStrings("10%", validated.actions[1].crop.x1.?);
            // The shapes land before the filter, in script order, so an @undo counts what the script does.
            try testing.expect(validated.actions[2] == .layout);
            try testing.expectEqual(opplan.FilterMode.custom, validated.actions[3].filter.mode);
        },
        .invalid => |msg| {
            defer gpa.free(msg);
            std.debug.print("the script plan did not validate: {s}\n", .{msg});
            return error.TestUnexpectedResult;
        },
    }
}

test "a script with an error plans nothing at all, and says why" {
    const gpa = testing.allocator;
    var threaded = std.Io.Threaded.init(gpa, .{});
    defer threaded.deinit();

    var s = try scriptCore.Script.parse("@source a.png:\n  @crop\n  @save\n");
    defer s.deinit();
    try testing.expect(s.hasErrors());

    var out: std.Io.Writer.Allocating = .init(gpa);
    defer out.deinit();
    try plan.writeEnvelope(gpa, threaded.io(), &out.writer, s, .{}, "s.stc");

    var parsed = try std.json.parseFromSlice(std.json.Value, gpa, out.written(), .{});
    defer parsed.deinit();
    try testing.expectEqual(@as(usize, 0), parsed.value.object.get("blocks").?.array.items.len);
    try testing.expect(parsed.value.object.get("diagnostics").?.array.items.len > 0);
}

test "the plan's action cap is the registry's own MAX_ACTIONS" {
    try testing.expectEqual(@as(f64, @floatFromInt(plan.MAX_ACTIONS)), opSchema.get().limitNamed("MAX_ACTIONS"));
}

test "a glob over two sizes plans each input against its own, and keeps the first-input fields" {
    const gpa = testing.allocator;
    var threaded = std.Io.Threaded.init(gpa, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();

    const names = [_][]const u8{ "stencil_plan_sizes_a.png", "stencil_plan_sizes_b.png" };
    const sample = try dir.readFileAlloc(io, "tests/fixtures/sample.png", gpa, .limited(1 << 20));
    defer gpa.free(sample);
    try dir.writeFile(io, .{ .sub_path = names[0], .data = sample }); // 16x12
    defer dir.deleteFile(io, names[0]) catch {};
    const px = try gpa.alloc(u8, 40 * 20 * 4);
    defer gpa.free(px);
    @memset(px, 255);
    const big = try image.encode(gpa, .{ .width = 40, .height = 20, .pixels = px }, .png);
    defer gpa.free(big);
    try dir.writeFile(io, .{ .sub_path = names[1], .data = big });
    defer dir.deleteFile(io, names[1]) catch {};

    var s = try scriptCore.Script.parse("@source stencil_plan_sizes_*.png:\n  @line (0,0) (50%,50%)\n  @save out/\n");
    defer s.deinit();
    var env: std.Io.Writer.Allocating = .init(gpa);
    defer env.deinit();
    try plan.writeEnvelope(gpa, io, &env.writer, s, .{}, "s.stc");
    var parsed = try std.json.parseFromSlice(std.json.Value, gpa, env.written(), .{});
    defer parsed.deinit();
    const block = parsed.value.object.get("blocks").?.array.items[0].object;

    // The block's own dims and plans still describe the first input, as they always have.
    try testing.expectEqual(@as(i64, 16), block.get("dims").?.object.get("width").?.integer);
    const first = block.get("plans").?.array.items[0].object.get("actions").?.array.items;
    const per = block.get("perInput").?.array.items;
    try testing.expectEqual(@as(usize, 2), per.len);
    const widths = [_]i64{ 16, 40 };
    const half_x = [_]i64{ 8, 20 };
    for (per, 0..) |entry, k| {
        const e = entry.object;
        try testing.expect(std.mem.endsWith(u8, e.get("input").?.string, names[k]));
        try testing.expectEqual(widths[k], e.get("dims").?.object.get("width").?.integer);
        const acts = e.get("plans").?.array.items[0].object.get("actions").?.array.items;
        try testing.expectEqualStrings(e.get("input").?.string, acts[0].object.get("path").?.string);
        const line = acts[actionAt(acts, "layout", null).?].object.get("lines").?.array.items[0];
        try testing.expectEqual(half_x[k], line.object.get("points").?.array.items[1].object.get("x").?.integer);
        const saves = e.get("saves").?.array.items;
        try testing.expectEqual(@as(usize, 1), saves.len);
        try testing.expectEqualStrings(e.get("input").?.string, saves[0].object.get("input").?.string);
    }
    // The first entry is exactly the block-level plan.
    const again = try std.json.Stringify.valueAlloc(gpa, per[0].object.get("plans").?, .{});
    defer gpa.free(again);
    const orig = try std.json.Stringify.valueAlloc(gpa, block.get("plans").?, .{});
    defer gpa.free(orig);
    try testing.expectEqualStrings(orig, again);
    try testing.expect(first.len > 0);
}

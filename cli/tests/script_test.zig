//! End-to-end `.stc` script runs: the save-naming matrix, directory and glob sources,
//! output confinement, the --script-check line grammar and the --script-plan envelope.
const std = @import("std");
const testing = std.testing;

const check = @import("../src/script/check.zig");
const image = @import("../src/image.zig");
const opSchema = @import("../src/llm/opSchema.zig");
const opplan = @import("../src/llm/opplan.zig");
const plan = @import("../src/script/plan.zig");
const report = @import("../src/report.zig");
const save = @import("../src/script/save.zig");
const scriptCore = @import("../src/scriptCore.zig");
const sources = @import("../src/script/sources.zig");
const steps = @import("../src/pipeline/steps.zig");

test "the save matrix: bare, directory, named and exact targets" {
    const gpa = testing.allocator;
    const Case = struct { target: []const u8, want: []const u8 };
    const cases = [_]Case{
        .{ .target = "", .want = "shots/a-stencil.png" },
        .{ .target = "out/", .want = "out/a-stencil.png" },
        .{ .target = "final", .want = "final.png" },
        .{ .target = "out/exact.jpg", .want = "out/exact.jpg" },
    };
    for (cases) |c| {
        const got = try save.resolveTarget(gpa, c.target, "shots/a.png", null, .png);
        defer gpa.free(got);
        try testing.expectEqualStrings(c.want, got);
    }
}

test "a glob picks only the matching media in its own directory" {
    try testing.expect(sources.globMatch("a*.png", "ab.png"));
    try testing.expect(!sources.globMatch("a*.png", "b.png"));
    try testing.expect(sources.globMatch("[bc].png", "b.png"));
    try testing.expect(!sources.globMatch("[bc].png", "a.png"));
}

test "--script-check prints the line an editor parses, and nothing when clean" {
    var buf: [2048]u8 = undefined;

    var bad: std.Io.Writer = .fixed(&buf);
    try testing.expect(try check.checkInto(&bad, "@source a.png:\n  @crp 10%\n", "s.stc"));
    try testing.expectEqualStrings(
        "s.stc:2:3: error: unknown directive '@crp' — did you mean '@crop'? [E_UNKNOWN_DIRECTIVE]\n",
        bad.buffered(),
    );

    var ok: std.Io.Writer = .fixed(&buf);
    try testing.expect(!try check.checkInto(&ok, "@source a.png:\n  @crop 10%\n  @save o.png\n", "s.stc"));
    try testing.expectEqualStrings("", ok.buffered());
}

test "the lowered stream is what the runner walks: open, edits, save" {
    var s = try scriptCore.Script.parse(
        "@source shots/a.png:\n  @crop 10%\n  @rect (1,1) (2,2)\n  @save\n",
    );
    defer s.deinit();
    try testing.expect(!s.hasErrors());
    try testing.expectEqual(@as(u32, 4), s.opCount());
    try testing.expectEqual(scriptCore.OpKind.open, s.op(0).?.kind);
    try testing.expectEqual(scriptCore.OpKind.crop, s.op(1).?.kind);
    try testing.expectEqual(scriptCore.OpKind.rect, s.op(2).?.kind);
    try testing.expectEqual(scriptCore.OpKind.save, s.op(3).?.kind);
    try testing.expectEqualStrings("", s.opStr(3, 0)); // a bare @save
}

test "undo is resolved before the runner ever sees it" {
    var s = try scriptCore.Script.parse(
        "@source a.png:\n  @filter bw\n  @rect (1,1) (2,2)\n  @undo\n  @save o.png\n",
    );
    defer s.deinit();
    var undos: usize = 0;
    var rects: usize = 0;
    var i: u32 = 0;
    while (i < s.opCount()) : (i += 1) {
        switch (s.op(i).?.kind) {
            .undo => undos += 1,
            .rect => rects += 1,
            else => {},
        }
    }
    try testing.expectEqual(@as(usize, 1), undos);
    try testing.expectEqual(@as(usize, 1), rects);
}

test "output confinement refuses traversal always and absolutes under the flag" {
    try testing.expectError(save.Error.SaveTraversal, save.guard("../out.png", false));
    try testing.expectError(save.Error.SaveOutsideCwd, save.guard("/tmp/out.png", true));
    try save.guard("out/x.png", true);
}

test "a block names its source kind so the runner knows how to open it" {
    const Case = struct { spec: []const u8, kind: scriptCore.SourceKind };
    const cases = [_]Case{
        .{ .spec = "a.png", .kind = .file },
        .{ .spec = "https://example.com/a.png", .kind = .url },
        .{ .spec = "shots/", .kind = .dir },
        .{ .spec = "shots/a*.png", .kind = .glob },
    };
    for (cases) |c| {
        const src = try std.fmt.allocPrint(testing.allocator, "@source {s}:\n  @filter bw\n", .{c.spec});
        defer testing.allocator.free(src);
        var s = try scriptCore.Script.parse(src);
        defer s.deinit();
        try testing.expectEqual(c.kind, s.block(0).?.kind);
        try testing.expectEqualStrings(c.spec, s.block(0).?.source);
    }
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
            // The shapes flush at the @save, exactly as run.zig burns them — after the filter.
            try testing.expectEqual(opplan.FilterMode.custom, validated.actions[2].filter.mode);
            try testing.expect(validated.actions[3] == .layout);
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

test "a save that cannot be written says so instead of exiting silently" {
    const gpa = testing.allocator;
    var threaded = std.Io.Threaded.init(gpa, .{});
    defer threaded.deinit();

    const Cap = struct {
        var sev: report.Severity = .plain;
        var buf: [256]u8 = undefined;
        var len: usize = 0;
        fn take(_: *anyopaque, s: report.Severity, text: []const u8) void {
            sev = s;
            len = @min(text.len, buf.len);
            @memcpy(buf[0..len], text[0..len]);
        }
    };
    var unused: u8 = 0;
    report.install(.{ .ctx = @ptrCast(&unused), .emitFn = Cap.take });
    defer report.uninstall();

    const img: image.Rgba8 = .{ .width = 1, .height = 1, .pixels = try gpa.dupe(u8, &[_]u8{ 0, 0, 0, 255 }) };
    defer gpa.free(img.pixels);

    try testing.expectError(
        error.FileNotFound,
        steps.writeOutputLabeled(gpa, threaded.io(), img, "no-such-dir/out.png", .png, ""),
    );
    try testing.expectEqual(report.Severity.err, Cap.sev);
    try testing.expect(std.mem.startsWith(u8, Cap.buf[0..Cap.len], "could not write no-such-dir/out.png"));
}

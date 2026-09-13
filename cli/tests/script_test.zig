//! End-to-end `.stc` script runs: the save-naming matrix, directory and glob sources,
//! output confinement, and the --script-check line grammar.
const std = @import("std");
const testing = std.testing;

const check = @import("../src/script/check.zig");
const save = @import("../src/script/save.zig");
const scriptCore = @import("../src/scriptCore.zig");
const sources = @import("../src/script/sources.zig");

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

//! End-to-end `.stc` script runs: the save-naming matrix, directory and glob sources, the
//! lowered stream and an @undo reaching the pixels, output confinement, a save that cannot be
//! written, and the --script-check line grammar.
const std = @import("std");
const testing = std.testing;

const check = @import("../../src/script/check.zig");
const image = @import("../../src/media/image.zig");
const report = @import("../../src/app/report.zig");
const run = @import("../../src/script/run.zig");
const save = @import("../../src/script/save.zig");
const scriptCore = @import("../../src/script/core.zig");
const sources = @import("../../src/script/sources.zig");
const steps = @import("../../src/pipeline/steps.zig");
const pixelsOf = @import("pixels.zig").pixelsOf;

test {
    _ = @import("clobber_test.zig"); // reaches the test build through this file
}

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

test "the lowered stream carries the undo the runner has to execute" {
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

test "an @undo reaches the pixels: the save after it has no rect in it" {
    const gpa = testing.allocator;
    var threaded = std.Io.Threaded.init(gpa, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();

    const stc = "stencil_script_undo.stc";
    const with_rect = "stencil_script_undo_one.png";
    const undone = "stencil_script_undo_two.png";
    const filter_only = "stencil_script_undo_three.png";
    try dir.writeFile(io, .{ .sub_path = stc, .data = "@source ../common/samples/sample.png:\n" ++
        "    @filter bw\n    @rect (0,0) (9,9)\n    @save " ++ with_rect ++ "\n" ++
        "    @undo\n    @save " ++ undone ++ "\n" ++
        "@source ../common/samples/sample.png:\n    @filter bw\n    @save " ++ filter_only ++ "\n" });
    defer dir.deleteFile(io, stc) catch {};
    defer dir.deleteFile(io, with_rect) catch {};
    defer dir.deleteFile(io, undone) catch {};
    defer dir.deleteFile(io, filter_only) catch {};

    try run.run(gpa, io, .{}, stc);

    const drawn = try pixelsOf(gpa, io, with_rect);
    defer gpa.free(drawn);
    const rewound = try pixelsOf(gpa, io, undone);
    defer gpa.free(rewound);
    const plain = try pixelsOf(gpa, io, filter_only);
    defer gpa.free(plain);

    try testing.expect(!std.mem.eql(u8, drawn, rewound)); // the first save drew the rect
    try testing.expectEqualSlices(u8, plain, rewound); // the second is the filter alone
}

test "output confinement refuses traversal always and absolutes under the flag" {
    try testing.expectError(save.Error.SaveTraversal, save.guard(testing.io, "../out.png", false));
    try testing.expectError(save.Error.SaveOutsideCwd, save.guard(testing.io, "/tmp/out.png", true));
    try save.guard(testing.io, "out/x.png", true);
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

//! `--no-clobber` over a script: a `--script` run refuses before its first write when any
//! `@save` would land on a file that is already there, a destination the run itself wrote is
//! its own, and `--script-emit` refuses an existing output before reading the script.
const std = @import("std");
const testing = std.testing;

const emit = @import("../../src/script/emit.zig");
const report = @import("../../src/app/report.zig");
const run = @import("../../src/script/run.zig");
const save = @import("../../src/script/save.zig");

/// The last line reported, so a refusal is read back in the words the adapters parse.
const Last = struct {
    var buf: [256]u8 = undefined;
    var len: usize = 0;
    fn take(_: *anyopaque, _: report.Severity, line: []const u8) void {
        len = @min(line.len, buf.len);
        @memcpy(buf[0..len], line[0..len]);
    }
    fn text() []const u8 {
        return buf[0..len];
    }
};
var sink_ctx: u8 = 0;

const STC = "stencil_clobber_run.stc";
const FIRST = "stencil_clobber_first.png";
const TAKEN = "stencil_clobber_taken";

fn cleanup(io: std.Io) void {
    for ([_][]const u8{ STC, FIRST, TAKEN ++ ".png" }) |p| std.Io.Dir.cwd().deleteFile(io, p) catch {};
}

test "--no-clobber stops a --script run before its first write, on the name the save fills in" {
    const gpa = testing.allocator;
    var threaded = std.Io.Threaded.init(gpa, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();
    defer cleanup(io);

    // Block 0 would write FIRST; block 1's `@save TAKEN` lands on TAKEN.png, which exists.
    try dir.writeFile(io, .{ .sub_path = STC, .data = "@source ../common/samples/sample.png:\n    @filter bw\n" ++
        "    @save " ++ FIRST ++ "\n@source ../common/samples/sample.png:\n    @save " ++ TAKEN ++ "\n" });
    try dir.writeFile(io, .{ .sub_path = TAKEN ++ ".png", .data = "keep" });

    report.install(.{ .ctx = @ptrCast(&sink_ctx), .emitFn = Last.take });
    defer report.uninstall();
    try testing.expectError(error.OutputExists, run.run(gpa, io, .{ .no_clobber = true }, STC));
    try testing.expectEqualStrings("--no-clobber: '" ++ TAKEN ++ ".png' already exists\n", Last.text());
    try testing.expectError(error.FileNotFound, dir.access(io, FIRST, .{}));
    var kept: [8]u8 = undefined;
    try testing.expectEqualStrings("keep", try dir.readFile(io, TAKEN ++ ".png", &kept));

    // Without the flag nothing changes: both saves write, the second over the old file.
    try run.run(gpa, io, .{}, STC);
    try dir.access(io, FIRST, .{});
    try testing.expect((try dir.statFile(io, TAKEN ++ ".png", .{})).size > 4);
}

test "a destination the run itself writes twice is its own, not a clobber" {
    const gpa = testing.allocator;
    var threaded = std.Io.Threaded.init(gpa, .{});
    defer threaded.deinit();
    const io = threaded.io();
    defer cleanup(io);

    try std.Io.Dir.cwd().writeFile(io, .{ .sub_path = STC, .data = "@source ../common/samples/sample.png:\n" ++
        "    @save " ++ FIRST ++ "\n    @filter bw\n    @save " ++ FIRST ++ "\n" });
    try run.run(gpa, io, .{ .no_clobber = true }, STC);
    try std.Io.Dir.cwd().access(io, FIRST, .{});
}

test "a destination no precheck saw is checked at its write" {
    const gpa = testing.allocator;
    var threaded = std.Io.Threaded.init(gpa, .{});
    defer threaded.deinit();
    const io = threaded.io();
    defer cleanup(io);
    try std.Io.Dir.cwd().writeFile(io, .{ .sub_path = TAKEN ++ ".png", .data = "keep" });

    report.install(.{ .ctx = @ptrCast(&sink_ctx), .emitFn = Last.take });
    defer report.uninstall();
    var on: save.Clobber = .{ .on = true };
    defer on.deinit(gpa);
    try testing.expectError(error.OutputExists, on.allow(gpa, io, TAKEN, .png));
    try on.allow(gpa, io, FIRST, .png); // absent: allowed, and remembered as this run's own
    try testing.expect(on.checked.contains(FIRST));
    var off: save.Clobber = .{ .on = false };
    try off.allow(gpa, io, TAKEN, .png);
}

test "--script-emit with --no-clobber refuses an existing output before reading the script" {
    const gpa = testing.allocator;
    var threaded = std.Io.Threaded.init(gpa, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();
    const out = "stencil_clobber_emit.py";
    defer dir.deleteFile(io, out) catch {};
    defer dir.deleteFile(io, STC) catch {};
    try dir.writeFile(io, .{ .sub_path = out, .data = "keep" });

    report.install(.{ .ctx = @ptrCast(&sink_ctx), .emitFn = Last.take });
    defer report.uninstall();
    // The script does not even exist: the refusal comes first.
    try testing.expectError(error.OutputExists, emit.runGuarded(gpa, io, "no-such.stc", out, .{ .no_clobber = true }));
    try testing.expectEqualStrings("--no-clobber: '" ++ out ++ "' already exists\n", Last.text());

    try dir.writeFile(io, .{ .sub_path = STC, .data = "@source a.png:\n    @filter bw\n    @save\n" });
    try emit.run(gpa, io, STC, out, false);
    try testing.expect((try dir.statFile(io, out, .{})).size > 4);
}

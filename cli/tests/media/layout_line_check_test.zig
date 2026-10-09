// A user-named layout's line fields as the registry's layout rule reads them: `points` must be an
// array, thickness and pointSize a number >= 0 (0 and fractions valid). The one-shot, `--script`'s
// loader and the console's /apply refuse such a line in one wording; the drawing paths read past it.
const std = @import("std");
const layout = @import("../../src/media/layout.zig");
const pipeline = @import("../../src/pipeline.zig");
const console = @import("../../src/console.zig");
const report = @import("../../src/app/report.zig");
const logo = @import("../../src/app/logo.zig");
const Capture = @import("../console/console_harness.zig").Capture;
const testing = std.testing;

fn badOf(json: []const u8) !?layout.BadLine {
    var doc = try layout.parse(testing.allocator, json);
    defer doc.deinit();
    return doc.bad_line;
}

test "bad_line names the first line whose points or sizes the rule refuses" {
    var buf: [96]u8 = undefined;
    const pts = (try badOf("{\"lines\":[{\"points\":[]},{\"points\":\"nope\"}]}")).?;
    try testing.expectEqualStrings("lines[1].points must be an array", pts.describe(&buf));
    const thick = (try badOf("{\"lines\":[{\"points\":[],\"thickness\":-1}]}")).?;
    try testing.expectEqualStrings("lines[0].thickness must be a number >= 0", thick.describe(&buf));
    const size = (try badOf("{\"lines\":[{\"points\":[],\"pointSize\":-0.5}]}")).?;
    try testing.expectEqualStrings("lines[0].pointSize must be a number >= 0", size.describe(&buf));
    // Zero, fractions, absent fields and a non-number (the tolerant default) all stand.
    try testing.expect(try badOf("{\"lines\":[{\"points\":[],\"thickness\":0,\"pointSize\":0.5}]}") == null);
    try testing.expect(try badOf("{\"lines\":[{\"thickness\":\"abc\"}]}") == null);
    // The drawing paths still read the document: the lines are there, bad one included.
    var doc = try layout.parse(testing.allocator, "{\"lines\":[{\"points\":[{\"x\":1,\"y\":1}],\"thickness\":-1}]}");
    defer doc.deinit();
    try testing.expectEqual(@as(usize, 1), doc.lines.len);
}

const Errs = struct {
    var buf: [256]u8 = undefined;
    var len: usize = 0;
    fn take(_: *anyopaque, sev: report.Severity, text: []const u8) void {
        if (sev != .err) return;
        const n = @min(text.len, buf.len);
        @memcpy(buf[0..n], text[0..n]);
        len = n;
    }
};

test "the one-shot and --script's loader refuse a bad line in one error line" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();
    const lay = "stencil_bad_line_layout.json";
    const out = "stencil_bad_line.png";
    try dir.writeFile(io, .{ .sub_path = lay, .data = "{\"lines\":[{\"points\":[{\"x\":1,\"y\":1}],\"thickness\":-2}]}" });
    defer dir.deleteFile(io, lay) catch {};
    var unused: u8 = 0;
    report.install(.{ .ctx = @ptrCast(&unused), .emitFn = Errs.take });
    defer report.uninstall();

    const opts = @import("../../src/args.zig").Options{ .blank = .{ .width = 4, .height = 4 }, .layout = lay, .output = out };
    try testing.expectError(error.InvalidLayoutLine, pipeline.run(testing.allocator, io, opts));
    try testing.expectEqualStrings("could not read layout 'stencil_bad_line_layout.json' (lines[0].thickness must be a number >= 0)\n", Errs.buf[0..Errs.len]);
    try testing.expectError(error.FileNotFound, dir.access(io, out, .{}));
    Errs.len = 0;
    try testing.expectError(error.InvalidLayoutLine, pipeline.loadLayoutDoc(testing.allocator, io, lay));
    try testing.expect(std.mem.indexOf(u8, Errs.buf[0..Errs.len], "lines[0].thickness") != null);
}

test "console /apply refuses a bad line in the same words and adds nothing" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();
    const lay = "stencil_bad_line_apply.json";
    try dir.writeFile(io, .{ .sub_path = lay, .data = "{\"lines\":[{\"points\":\"nope\"}]}" });
    defer dir.deleteFile(io, lay) catch {};
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();

    var session = console.Session{ .gpa = a };
    defer session.deinit();
    _ = try console.handle(&session, io, "/blank 8 8 white");
    try testing.expect(!try console.handle(&session, io, "/apply " ++ lay));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "could not read layout 'stencil_bad_line_apply.json' (lines[0].points must be an array)") != null);
    try testing.expectEqualStrings("[]", session.state().lines());
}

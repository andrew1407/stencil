// The one-shot pipeline's refusals as an adapter reads them: a layout that is not a layout object
// says so in one `error:` line before exit 1, and a --filter or a whole number that is none is an
// argv error that names the flag and what it got.
const std = @import("std");
const pipeline = @import("../../src/pipeline.zig");
const args = @import("../../src/args.zig");
const image = @import("../../src/media/image.zig");
const report = @import("../../src/app/report.zig");
const logo = @import("../../src/app/logo.zig");
const testing = std.testing;

const Errs = struct {
    var buf: [512]u8 = undefined;
    var len: usize = 0;
    var count: usize = 0;
    fn take(_: *anyopaque, sev: report.Severity, text: []const u8) void {
        if (sev != .err) return;
        const n = @min(text.len, buf.len);
        @memcpy(buf[0..n], text[0..n]);
        len = n;
        count += 1;
    }
};

fn runCapturing(io: std.Io, opts: args.Options) !void {
    var unused: u8 = 0;
    Errs.count = 0;
    report.install(.{ .ctx = @ptrCast(&unused), .emitFn = Errs.take });
    defer report.uninstall();
    return pipeline.run(testing.allocator, io, opts);
}

test "pipeline: a malformed or non-object layout is one error line, nothing written" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();
    const lay = "stencil_run_bad_layout.json";
    const out = "stencil_run_bad_layout.png";
    defer dir.deleteFile(io, lay) catch {};
    const cases = [_]struct { json: []const u8, why: []const u8 }{
        .{ .json = "{\"lines\":[", .why = "UnexpectedEndOfInput" },
        .{ .json = "[]", .why = "not a JSON object" },
        .{ .json = "null", .why = "not a JSON object" },
        .{ .json = "\"x\"", .why = "not a JSON object" },
    };
    for (cases) |c| {
        try dir.writeFile(io, .{ .sub_path = lay, .data = c.json });
        const opts = args.Options{ .blank = .{ .width = 8, .height = 8 }, .layout = lay, .output = out };
        try testing.expect(std.meta.isError(runCapturing(io, opts)));
        var want: [128]u8 = undefined;
        const line = try std.fmt.bufPrint(&want, "could not read layout '{s}' ({s})\n", .{ lay, c.why });
        try testing.expectEqualStrings(line, Errs.buf[0..Errs.len]);
        try testing.expectEqual(@as(usize, 1), Errs.count);
        try testing.expectError(error.FileNotFound, dir.access(io, out, .{}));
    }
}

test "argv: --filter refuses what is no filter mode or colour, empty included" {
    for ([_][:0]const u8{ "#ggg", "notacolor", "" }) |bad| {
        const argv = [_][:0]const u8{ "--blank", "8", "8", "--filter", bad, "o.png" };
        try testing.expectError(args.Error.BadValue, args.parse(&argv));
    }
    for ([_][:0]const u8{ "sepia", "#7c3aed", "BW", "none" }) |ok| {
        const argv = [_][:0]const u8{ "--blank", "8", "8", "--filter", ok, "o.png" };
        try testing.expectEqualStrings(ok, (try args.parse(&argv)).filter.?);
    }
}

test "argv: a bad whole number is one error line naming the flag and what it got" {
    const Cap = struct {
        buf: std.ArrayList(u8) = .empty,
        fn sink(ctx: *anyopaque, bytes: []const u8) void {
            const self: *@This() = @ptrCast(@alignCast(ctx));
            self.buf.appendSlice(testing.allocator, bytes) catch {};
        }
    };
    const cases = [_]struct { argv: []const [:0]const u8, line: []const u8 }{
        .{ .argv = &.{ "-r", "1.5", "o.png" }, .line = "error: --rotate expects a whole number, got '1.5'\n" },
        .{ .argv = &.{ "--frame", "x", "o.png" }, .line = "error: --frame expects a whole number, got 'x'\n" },
        .{ .argv = &.{ "--blank", "8", "8", "--thumbnail", "-4", "o.png" }, .line = "error: --thumbnail expects a whole number, got '-4'\n" },
    };
    defer logo.init(false, false);
    logo.init(false, false);
    for (cases) |c| {
        var cap = Cap{};
        defer cap.buf.deinit(testing.allocator);
        logo.setSink(Cap.sink, &cap);
        defer logo.clearSink();
        try testing.expectError(args.Error.BadNumber, args.parse(c.argv));
        try testing.expectEqualStrings(c.line, cap.buf.items);
    }
}

test "pipeline: a named filter in any case filters, a colour tints, neither turns black" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();
    const out = "stencil_run_filter.png";
    defer dir.deleteFile(io, out) catch {};
    const cases = [_]struct { filter: []const u8, px: [3]u8 }{
        .{ .filter = "BW", .px = .{ 54, 54, 54 } }, // rec709 luma of pure red, truncated
        .{ .filter = "#7c3aed", .px = .{ 0x7c, 0x3a, 0xed } }, // a dark pixel takes the tint
    };
    for (cases) |c| {
        const color: []const u8 = if (c.filter[0] == '#') "#000000" else "#ff0000";
        try runCapturing(io, .{ .blank = .{ .width = 4, .height = 4, .color = color }, .filter = c.filter, .output = out });
        const bytes = try dir.readFileAlloc(io, out, testing.allocator, .limited(1 << 20));
        defer testing.allocator.free(bytes);
        var img = try image.decode(testing.allocator, bytes);
        defer img.deinit(testing.allocator);
        try testing.expectEqualSlices(u8, &c.px, img.pixels[0..3]);
    }
}

test "pipeline: a browser layout's custom filter tints with its filterColor, not black" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();
    const lay = "stencil_run_custom_layout.json";
    const out = "stencil_run_custom.png";
    defer dir.deleteFile(io, lay) catch {};
    defer dir.deleteFile(io, out) catch {};
    try dir.writeFile(io, .{ .sub_path = lay, .data = "{\"imageFilter\":\"custom\",\"filterColor\":\"#7c3aed\",\"lines\":[]}" });
    try runCapturing(io, .{ .blank = .{ .width = 4, .height = 4, .color = "#000000" }, .layout = lay, .output = out });
    const bytes = try dir.readFileAlloc(io, out, testing.allocator, .limited(1 << 20));
    defer testing.allocator.free(bytes);
    var img = try image.decode(testing.allocator, bytes);
    defer img.deinit(testing.allocator);
    try testing.expectEqualSlices(u8, &.{ 0x7c, 0x3a, 0xed }, img.pixels[0..3]);
}

// --thumbnail through the whole one-shot pipeline: the file written and the `wrote` line both
// carry the shrunk size, a solid colour survives it exactly, and a small result is never grown.
const std = @import("std");
const pipeline = @import("../../src/pipeline.zig");
const args = @import("../../src/args.zig");
const image = @import("../../src/media/image.zig");
const report = @import("../../src/app/report.zig");
const testing = std.testing;
const sample = @embedFile("../fixtures/sample.png");

const Wrote = struct {
    var buf: [256]u8 = undefined;
    var len: usize = 0;
    fn take(_: *anyopaque, _: report.Severity, text: []const u8) void {
        const n = @min(text.len, buf.len);
        @memcpy(buf[0..n], text[0..n]);
        len = n;
    }
};

fn runTo(a: std.mem.Allocator, io: std.Io, opts: args.Options, out: []const u8) !image.Rgba8 {
    var unused: u8 = 0;
    report.install(.{ .ctx = @ptrCast(&unused), .emitFn = Wrote.take });
    defer report.uninstall();
    try pipeline.run(a, io, opts);
    const bytes = try std.Io.Dir.cwd().readFileAlloc(io, out, a, .limited(1 << 20));
    defer a.free(bytes);
    return image.decode(a, bytes);
}

test "pipeline: --thumbnail shrinks the written result and the wrote line says so" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();
    const in = "stencil_thumb_in.png";
    const out = "stencil_thumb_out.png";
    try dir.writeFile(io, .{ .sub_path = in, .data = sample });
    defer dir.deleteFile(io, in) catch {};
    defer dir.deleteFile(io, out) catch {};

    // The 16x12 fixture, rotated to 12x16, then fitted into 8 px: 6x8.
    var img = try runTo(a, io, .{ .input = in, .rotate = 1, .thumbnail = 8, .output = out }, out);
    defer img.deinit(a);
    try testing.expectEqual(@as(usize, 6), img.width);
    try testing.expectEqual(@as(usize, 8), img.height);
    try testing.expect(std.mem.startsWith(u8, Wrote.buf[0..Wrote.len], "wrote stencil_thumb_out.png (6x8 px · A4 21×29.7cm)"));

    // Already inside the bound: the pipeline writes the full size, as without the flag.
    var same = try runTo(a, io, .{ .input = in, .thumbnail = 100, .output = out }, out);
    defer same.deinit(a);
    try testing.expectEqual(@as(usize, 16), same.width);
    try testing.expectEqual(@as(usize, 12), same.height);
}

test "pipeline: --thumbnail keeps a solid colour exact" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const out = "stencil_thumb_blank.png";
    defer std.Io.Dir.cwd().deleteFile(io, out) catch {};

    const opts = args.Options{ .blank = .{ .width = 90, .height = 30, .color = "#336699" }, .thumbnail = 20, .output = out };
    var img = try runTo(a, io, opts, out);
    defer img.deinit(a);
    try testing.expectEqual(@as(usize, 20), img.width);
    try testing.expectEqual(@as(usize, 7), img.height); // 30 * 20 / 90 = 6.67
    var i: usize = 0;
    while (i < img.pixels.len) : (i += 4) try testing.expectEqualSlices(u8, &.{ 0x33, 0x66, 0x99, 255 }, img.pixels[i..][0..4]);
}

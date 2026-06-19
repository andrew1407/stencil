// End-to-end: write fixtures to disk, run the full pipeline (crop + rotate + layout +
// filter override), then read the output back and check its dimensions.
const std = @import("std");
const pipeline = @import("../src/pipeline.zig");
const args = @import("../src/args.zig");
const image = @import("../src/image.zig");
const testing = std.testing;
const sample = @embedFile("fixtures/sample.png");
const layout_json = @embedFile("fixtures/layout.json");

test "pipeline: file in -> crop+rotate+layout+filter -> file out" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();

    const in = "stencil_e2e_in.png";
    const lay = "stencil_e2e_layout.json";
    const out = "stencil_e2e_out.png";
    try dir.writeFile(io, .{ .sub_path = in, .data = sample });
    try dir.writeFile(io, .{ .sub_path = lay, .data = layout_json });
    defer dir.deleteFile(io, in) catch {};
    defer dir.deleteFile(io, lay) catch {};
    defer dir.deleteFile(io, out) catch {};

    const opts = args.Options{
        .input = in,
        .crop = "x1=0% x2=50% y1=0% y2=100%",
        .rotate = 1,
        .layout = lay,
        .filter = "sepia", // overrides the layout's "bw"
        .output = out,
    };
    try pipeline.run(a, io, opts);

    const bytes = try dir.readFileAlloc(io, out, a, .limited(1 << 20));
    defer a.free(bytes);
    var img = try image.decode(a, bytes);
    defer img.deinit(a);
    // crop 50% of width 16 -> 8 wide, full height 12; rotate one quarter -> 12x8
    try testing.expectEqual(@as(usize, 12), img.width);
    try testing.expectEqual(@as(usize, 8), img.height);
}

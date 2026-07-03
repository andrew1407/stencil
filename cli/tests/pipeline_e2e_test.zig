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

test "pipeline: the wrote-line page label follows the effective page state" {
    const a = testing.allocator;

    // Precedence: an applied layout's pageSize beats --blank's pick, else "" (A4 default).
    try testing.expectEqualStrings("B5", pipeline.effectivePageName("B5", "A4"));
    try testing.expectEqualStrings("A6", pipeline.effectivePageName(null, "A6"));
    try testing.expectEqualStrings("", pipeline.effectivePageName(null, null));

    // A named format is oriented to the image (B5 is 17.6×25cm; a landscape image swaps it).
    const b5 = try pipeline.pageLabelAlloc(a, "B5", 0, 0, 800, 600);
    defer a.free(b5);
    try testing.expectEqualStrings("B5 25×17.6cm", b5);

    // A custom page reports its real cm dims — never an A4 fallback.
    const custom = try pipeline.pageLabelAlloc(a, "custom", 10, 15, 378, 567);
    defer a.free(custom);
    try testing.expectEqualStrings("custom 10×15cm", custom);

    // Nothing picked → the A4-derived default oriented to the (portrait) image.
    const def = try pipeline.pageLabelAlloc(a, "", 0, 0, 600, 800);
    defer a.free(def);
    try testing.expectEqualStrings("A4 21×29.7cm", def);
}

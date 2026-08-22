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

test "pipeline: --layout-frame source re-maps layout points through crop/rotate; default current does not" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();

    // A single-point red dot at SOURCE (12,6) on a 16x12 white blank, cropped to the right
    // half (x1=8px; y given so the aspect derivation stays out) and rotated one quarter.
    const lay = "stencil_lf_layout.json";
    const out_src = "stencil_lf_src.png";
    const out_cur = "stencil_lf_cur.png";
    try dir.writeFile(io, .{ .sub_path = lay, .data = "{\"lines\":[{\"points\":[{\"x\":12,\"y\":6}],\"color\":\"#FF0000\",\"pointSize\":2,\"thickness\":2}]}" });
    defer dir.deleteFile(io, lay) catch {};
    defer dir.deleteFile(io, out_src) catch {};
    defer dir.deleteFile(io, out_cur) catch {};

    const base = args.Options{
        .blank = .{ .width = 16, .height = 12, .color = "white" },
        .crop = "x1=8px y1=0px",
        .rotate = 1,
        .layout = lay,
    };

    // source: (12,6) → crop (−8,0) → (4,6) → CW quarter of the 8x12 crop → (12−6,4) = (6,4).
    var opts = base;
    opts.layout_frame = .source;
    opts.output = out_src;
    try pipeline.run(a, io, opts);
    {
        const bytes = try dir.readFileAlloc(io, out_src, a, .limited(1 << 20));
        defer a.free(bytes);
        var img = try image.decode(a, bytes);
        defer img.deinit(a);
        try testing.expectEqual(@as(usize, 12), img.width);
        try testing.expectEqual(@as(usize, 8), img.height);
        const at = (4 * img.width + 6) * 4; // (6,4)
        try testing.expect(img.pixels[at] > 200 and img.pixels[at + 1] < 60); // red dot landed
    }

    // current (default): the same doc draws at literal (12,6) — outside the 12x8 result's
    // reachable dot spot (6,4), which stays white.
    opts = base;
    opts.output = out_cur;
    try pipeline.run(a, io, opts);
    {
        const bytes = try dir.readFileAlloc(io, out_cur, a, .limited(1 << 20));
        defer a.free(bytes);
        var img = try image.decode(a, bytes);
        defer img.deinit(a);
        const at = (4 * img.width + 6) * 4; // (6,4) — untouched white
        try testing.expect(img.pixels[at] > 200 and img.pixels[at + 1] > 200 and img.pixels[at + 2] > 200);
    }
}

test "pipeline: the filter runs under the layout — drawn lines keep their own colours" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();

    // A green dot on a red blank, greyscaled. The filter belongs to the picture, so the
    // background turns grey while the annotation stays green (the layering every other
    // surface renders: browser/desktop overlay, console + pystencil rebuild).
    const lay = "stencil_fo_layout.json";
    const out_flag = "stencil_fo_flag.png";
    const out_doc = "stencil_fo_doc.png";
    try dir.writeFile(io, .{
        .sub_path = lay,
        .data = "{\"filter\":\"bw\",\"lines\":[{\"points\":[{\"x\":8,\"y\":6}],\"color\":\"#00FF00\",\"pointSize\":3,\"thickness\":2}]}",
    });
    defer dir.deleteFile(io, lay) catch {};
    defer dir.deleteFile(io, out_flag) catch {};
    defer dir.deleteFile(io, out_doc) catch {};

    const base = args.Options{
        .blank = .{ .width = 16, .height = 12, .color = "red" },
        .layout = lay,
    };

    // Both routes to the same filter: the --filter flag, and the layout doc's own "filter".
    for ([_][]const u8{ out_flag, out_doc }, 0..) |out, i| {
        var opts = base;
        opts.output = out;
        if (i == 0) opts.filter = "bw";
        try pipeline.run(a, io, opts);

        const bytes = try dir.readFileAlloc(io, out, a, .limited(1 << 20));
        defer a.free(bytes);
        var img = try image.decode(a, bytes);
        defer img.deinit(a);

        const dot = (6 * img.width + 8) * 4; // (8,6)
        try testing.expect(img.pixels[dot + 1] > 200); // green kept
        try testing.expect(img.pixels[dot] < 60 and img.pixels[dot + 2] < 60);

        const bg = (1 * img.width + 1) * 4; // untouched corner: red → grey
        try testing.expectEqual(img.pixels[bg], img.pixels[bg + 1]);
        try testing.expectEqual(img.pixels[bg + 1], img.pixels[bg + 2]);
    }
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

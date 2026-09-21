// The one-shot `.stencil` mode (src/project_cli.zig): bundling a source into a project,
// rendering a project back out, the flag edits layered on top in pipeline order, and the
// paths that refuse. Real files in the cwd, no network.
const std = @import("std");
const args = @import("../src/args.zig");
const image = @import("../src/media/image.zig");
const project = @import("../src/project.zig");
const project_cli = @import("../src/project/cli.zig");
const testing = std.testing;

fn decodeFile(a: std.mem.Allocator, io: std.Io, path: []const u8) !image.Rgba8 {
    const bytes = try std.Io.Dir.cwd().readFileAlloc(io, path, a, .limited(4 << 20));
    defer a.free(bytes);
    return image.decode(a, bytes);
}

test "one-shot: --blank bundles a .stencil, which renders back out at its own size" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();

    const proj = "stencil_oneshot.stencil";
    const out = "stencil_oneshot_out.png";
    defer dir.deleteFile(io, proj) catch {};
    defer dir.deleteFile(io, out) catch {};

    try project_cli.runOneShot(a, io, .{ .blank = .{ .width = 16, .height = 12, .color = "red" }, .output = proj });

    // The bundle records the blank's own metadata, and carries a decodable image.
    const bytes = try dir.readFileAlloc(io, proj, a, .limited(4 << 20));
    defer a.free(bytes);
    var parsed = try project.parse(a, bytes);
    defer parsed.deinit();
    try testing.expectEqualStrings("blank", parsed.name);
    try testing.expect(parsed.blank);
    try testing.expectEqualStrings("red", parsed.blank_color);
    try testing.expectEqual(@as(i64, 16), parsed.image_w);

    // …and the project renders back out through the Session path at the same size.
    try project_cli.runOneShot(a, io, .{ .input = proj, .output = out });
    var img = try decodeFile(a, io, out);
    defer img.deinit(a);
    try testing.expectEqual(@as(usize, 16), img.width);
    try testing.expectEqual(@as(usize, 12), img.height);
}

test "one-shot: crop, rotate, layout and filter layer onto the source in pipeline order" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();

    const lay = "stencil_oneshot_layout.json";
    const proj = "stencil_oneshot_edit.stencil";
    const out = "stencil_oneshot_edit.png";
    try dir.writeFile(io, .{ .sub_path = lay, .data = "{\"lines\":[{\"points\":[{\"x\":2,\"y\":2}],\"color\":\"#00FF00\",\"pointSize\":3,\"thickness\":2}]}" });
    defer dir.deleteFile(io, lay) catch {};
    defer dir.deleteFile(io, proj) catch {};
    defer dir.deleteFile(io, out) catch {};

    try project_cli.runOneShot(a, io, .{ .blank = .{ .width = 16, .height = 12, .color = "red" }, .output = proj });
    try project_cli.runOneShot(a, io, .{
        .input = proj,
        .crop = "x1=0px x2=8px y1=0px y2=12px",
        .rotate = 1,
        .layout = lay,
        .filter = "bw", // the flag filter beats the project's own
        .output = out,
    });

    // crop to 8x12, then a quarter turn → 12x8.
    var img = try decodeFile(a, io, out);
    defer img.deinit(a);
    try testing.expectEqual(@as(usize, 12), img.width);
    try testing.expectEqual(@as(usize, 8), img.height);
    // The filter runs UNDER the layout: the background greys, the drawn dot keeps its green.
    const bg = (7 * img.width + 11) * 4;
    try testing.expectEqual(img.pixels[bg], img.pixels[bg + 1]);
    const dot = (2 * img.width + 2) * 4;
    try testing.expect(img.pixels[dot + 1] > 200 and img.pixels[dot] < 60);
}

test "one-shot: a custom --filter value is a tint colour, not a named mode" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();

    const proj = "stencil_oneshot_tint.stencil";
    const out = "stencil_oneshot_tint.png";
    defer dir.deleteFile(io, proj) catch {};
    defer dir.deleteFile(io, out) catch {};

    try project_cli.runOneShot(a, io, .{ .blank = .{ .width = 8, .height = 8, .color = "#808080" }, .output = proj });
    try project_cli.runOneShot(a, io, .{ .input = proj, .filter = "#ff0000", .output = out });

    var img = try decodeFile(a, io, out);
    defer img.deinit(a);
    // A mid grey duotoned through #ff0000: red saturates while g/b stay at the luma.
    // "bw" would have left all three equal — the value is a colour, not a named mode.
    try testing.expectEqual(@as(u8, 255), img.pixels[0]);
    try testing.expectEqual(@as(u8, 128), img.pixels[1]);
    try testing.expectEqual(@as(u8, 128), img.pixels[2]);
}

test "one-shot: no source, no output, and a confined path outside the cwd all refuse" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();

    const blank = args.Options{ .blank = .{ .width = 8, .height = 8, .color = "white" } };
    try testing.expectError(error.NoSource, project_cli.runOneShot(a, io, .{ .output = "x.stencil" }));
    try testing.expectError(error.NoOutput, project_cli.runOneShot(a, io, blank));

    var confined = blank;
    confined.confine_output = true;
    confined.output = "/tmp/stencil_oneshot_escape.stencil";
    try testing.expectError(error.UnsafeOutputPath, project_cli.runOneShot(a, io, confined));
}

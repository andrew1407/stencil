// Integration: console /rotate turns the drawn lines and the crop with the picture, as the
// browser's rotate does (core rotateEditQuarter + rotateLinePointsQuarter), and /undo turns
// them back. Driven through console.handle, the path the interactive loop uses.
const std = @import("std");
const console = @import("../../src/console.zig");
const core = @import("../../src/core.zig");
const testing = std.testing;

const layout_in = "stencil_console_rotate_lines.json";
const layout_json = "{\"lines\":[{\"points\":[{\"x\":2,\"y\":3},{\"x\":7,\"y\":1}],\"color\":\"#00ff00\",\"pointSize\":3,\"thickness\":2}]}";

fn has(session: *console.Session, needle: []const u8) bool {
    return std.mem.indexOf(u8, session.state().lines(), needle) != null;
}

fn isGreen(session: *console.Session, x: usize, y: usize) bool {
    const img = session.current().*;
    const i = (y * img.width + x) * 4;
    return img.pixels[i + 1] > 200 and img.pixels[i] < 60;
}

test "console: /rotate turns the drawn lines inside the view, and /undo turns them back" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();
    try dir.writeFile(io, .{ .sub_path = layout_in, .data = layout_json });
    defer dir.deleteFile(io, layout_in) catch {};

    var session = console.Session{ .gpa = a };
    defer session.deinit();
    _ = try console.handle(&session, io, "/blank 16 12 red");
    _ = try console.handle(&session, io, "/apply " ++ layout_in);
    const before = try a.dupe(u8, session.state().lines());
    defer a.free(before);

    // Right in 16x12: (x, y) → (12 − y, x).
    _ = try console.handle(&session, io, "/rotate 1");
    try testing.expect(has(&session, "{\"x\":9,\"y\":2},{\"x\":11,\"y\":7}"));
    try testing.expectEqual(@as(usize, 12), session.current().width);
    try testing.expect(isGreen(&session, 9, 2));

    _ = try console.handle(&session, io, "/undo");
    try testing.expectEqualStrings(before, session.state().lines());
    _ = try console.handle(&session, io, "/redo");
    try testing.expect(has(&session, "{\"x\":9,\"y\":2}"));

    // Left from there is the way back: (x, y) → (y, W − x) in the turned 12x16 view.
    _ = try console.handle(&session, io, "/rotate -1");
    try testing.expectEqualStrings(before, session.state().lines());
    try testing.expect(isGreen(&session, 2, 3));
}

test "console: under a crop the lines turn in the cropped view and the crop follows as core turns it" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();
    try dir.writeFile(io, .{ .sub_path = layout_in, .data = layout_json });
    defer dir.deleteFile(io, layout_in) catch {};

    var session = console.Session{ .gpa = a };
    defer session.deinit();
    _ = try console.handle(&session, io, "/blank 16 12 red");
    _ = try console.handle(&session, io, "/crop x1=0% x2=50% y1=0% y2=100%");
    _ = try console.handle(&session, io, "/apply " ++ layout_in);
    const crop = session.state().crop.?;

    _ = try console.handle(&session, io, "/rotate 1");
    const want = core.rotateEditQuarter(crop, 0, 16, 12, true);
    try testing.expectEqual(want.crop, session.state().crop.?);
    try testing.expectEqual(want.quarters, session.state().rotation);
    // Right in the 8x12 crop: (x, y) → (12 − y, x).
    try testing.expect(has(&session, "{\"x\":9,\"y\":2},{\"x\":11,\"y\":7}"));
    try testing.expect(isGreen(&session, 9, 2));

    _ = try console.handle(&session, io, "/rotate 3");
    try testing.expectEqual(crop, session.state().crop.?);
    try testing.expect(has(&session, "{\"x\":2,\"y\":3},{\"x\":7,\"y\":1}"));
}

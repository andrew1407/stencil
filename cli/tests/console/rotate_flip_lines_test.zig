// Integration: console /rotate and /flip carry the drawn lines and the crop with the picture, as
// the browser's do (core rotateEditQuarter + rotateLinePointsQuarter, mirrorEdit + mirrorLinePoints),
// /undo brings them back, and a flipped view reaches the server layout as `mirrored`.
const std = @import("std");
const console = @import("../../src/console.zig");
const core = @import("../../src/core.zig");
const testing = std.testing;

const layout_in = "stencil_console_rotate_flip_lines.json";

const checks = @import("line_checks.zig");
const layout_json = checks.layout_json;
const has = checks.has;
const isGreen = checks.isGreen;

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

test "console: /flip mirrors the drawn lines inside the view, and /undo mirrors them back" {
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

    // In a 16-wide view: (x, y) → (16 − x, y).
    _ = try console.handle(&session, io, "/flip");
    try testing.expect(has(&session, "{\"x\":14,\"y\":3},{\"x\":9,\"y\":1}"));
    try testing.expect(session.state().mirrored);
    try testing.expectEqual(@as(usize, 16), session.current().width);
    try testing.expect(isGreen(&session, 13, 3));

    const layout = try session.currentLayoutJson();
    defer a.free(layout);
    try testing.expect(std.mem.endsWith(u8, layout, ",\"mirrored\":true}"));

    _ = try console.handle(&session, io, "/undo");
    try testing.expectEqualStrings(before, session.state().lines());
    try testing.expect(!session.state().mirrored);

    // A turn then a flip: the flip negates the turn, as turn(q)·mirror = mirror·turn(−q).
    _ = try console.handle(&session, io, "/rotate 1");
    _ = try console.handle(&session, io, "/flip");
    try testing.expectEqual(@as(i32, 3), session.state().rotation);
    _ = try console.handle(&session, io, "/flip");
    try testing.expectEqual(@as(i32, 1), session.state().rotation);
    try testing.expect(!session.state().mirrored);
}

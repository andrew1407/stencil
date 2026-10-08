// Integration: console /flip mirrors the picture, the drawn lines and the crop as the browser's flip
// does (core mirrorEdit + mirrorLinePoints), /undo brings them back, and a flipped view reaches the
// server layout as `mirrored`. Driven through console.handle, the path the interactive loop uses.
const std = @import("std");
const console = @import("../../src/console.zig");
const testing = std.testing;

const layout_in = "stencil_console_flip_lines.json";
const layout_json = "{\"lines\":[{\"points\":[{\"x\":2,\"y\":3},{\"x\":7,\"y\":1}],\"color\":\"#00ff00\",\"pointSize\":3,\"thickness\":2}]}";

fn has(session: *console.Session, needle: []const u8) bool {
    return std.mem.indexOf(u8, session.state().lines(), needle) != null;
}

fn isGreen(session: *console.Session, x: usize, y: usize) bool {
    const img = session.current().*;
    const i = (y * img.width + x) * 4;
    return img.pixels[i + 1] > 200 and img.pixels[i] < 60;
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

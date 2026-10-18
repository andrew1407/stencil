//! The linked-file round trip: an edit here lands in the file at the prompt boundary, a layout
//! the desktop wrote arrives as one undo step, another picture reloads the session, and a
//! file that vanishes unlinks.
const std = @import("std");
const console = @import("../../src/console.zig");
const remoteEvents = @import("../../src/console/remoteEvents.zig");
const fileLink = @import("../../src/console/remoteEvents/fileLink.zig");
const codec = @import("../../src/project/codec.zig");
const logo = @import("../../src/app/logo.zig");
const harness = @import("console_harness.zig");
const Capture = harness.Capture;
const sample = harness.sample;
const testing = std.testing;

const path = "stencil_file_link.stencil";

fn rewrite(a: std.mem.Allocator, io: std.Io, image_bytes: []const u8, layout: []const u8) !void {
    const doc = try codec.build(a, .{ .name = "linked", .image_bytes = image_bytes, .image_ext = "png", .image_w = 16, .image_h = 12, .layout_json = layout });
    defer a.free(doc);
    try std.Io.Dir.cwd().writeFile(io, .{ .sub_path = path, .data = doc });
}

test "file link: edits are written, the desktop's layout is one undo step, a new picture reloads" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = console.Session{ .gpa = a };
    defer session.deinit();
    try std.Io.Dir.cwd().writeFile(io, .{ .sub_path = "stencil_file_link_in.png", .data = sample });
    defer std.Io.Dir.cwd().deleteFile(io, "stencil_file_link_in.png") catch {};
    defer std.Io.Dir.cwd().deleteFile(io, path) catch {};
    _ = try console.handle(&session, io, "/upload stencil_file_link_in.png");
    _ = try console.handle(&session, io, "/save " ++ path);
    try fileLink.link(&session, io, path);
    try testing.expect(!remoteEvents.pollFileLink(&session, io)); // nothing to do yet

    // An edit here reaches the file at the boundary.
    _ = try console.handle(&session, io, "/filter sepia");
    try testing.expect(session.file_link.?.dirty);
    try testing.expect(remoteEvents.pollFileLink(&session, io));
    try testing.expect(!session.file_link.?.dirty);
    const written = try std.Io.Dir.cwd().readFileAlloc(io, path, a, .limited(1 << 20));
    defer a.free(written);
    try testing.expect(std.mem.indexOf(u8, written, "\"imageFilter\":\"sepia\"") != null);

    // The desktop writes a layout over the same picture: one more history state, undoable.
    const before = session.stateCount();
    try rewrite(a, io, session.source_bytes.?, "{\"imageFilter\":\"bw\",\"lines\":[{\"points\":[{\"x\":1,\"y\":1},{\"x\":5,\"y\":5}],\"color\":\"#ff0000\",\"thickness\":1}]}");
    try testing.expect(remoteEvents.pollFileLink(&session, io));
    try testing.expectEqual(before + 1, session.stateCount());
    try testing.expectEqualStrings("bw", session.state().filter_mode);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "took the desktop's change") != null);
    _ = try console.handle(&session, io, "/undo");
    try testing.expectEqualStrings("sepia", session.state().filter_mode);
    try testing.expect(!remoteEvents.pollFileLink(&session, io)); // an undo is not an edit to write

    // Another picture: the session reloads from the file (history reset to its layout).
    var bigger = try a.alloc(u8, sample.len + 1);
    defer a.free(bigger);
    @memcpy(bigger[0..sample.len], sample);
    bigger[sample.len] = 0; // a trailing byte the decoder ignores, so the bytes differ
    try rewrite(a, io, bigger, "{\"imageFilter\":\"invert\"}");
    try testing.expect(remoteEvents.pollFileLink(&session, io));
    try testing.expectEqual(@as(usize, 1), session.stateCount());
    try testing.expectEqualStrings("invert", session.state().filter_mode);

    // The file vanishing unlinks with a note.
    try std.Io.Dir.cwd().deleteFile(io, path);
    try testing.expect(remoteEvents.pollFileLink(&session, io));
    try testing.expect(session.file_link == null);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "is gone") != null);
}

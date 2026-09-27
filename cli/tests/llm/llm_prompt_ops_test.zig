//! The expanded console op set, the history half: §2 undo/redo/reset, §10 clear, an empty §4
//! layout, and a variant's render recolouring the picture under the lines.
const std = @import("std");
const image = @import("../../src/media/image.zig");
const logo = @import("../../src/app/logo.zig");
const core = @import("../../src/core.zig");
const llm = @import("../../src/llm.zig");
const layout_mod = @import("../../src/media/layout.zig");
const Session = @import("../../src/console/session.zig").Session;
const fixture = @import("../../src/console/llm/fixture.zig");
const Capture = fixture.Capture;
const applyOne = fixture.applyOne;
const swallowPrint = fixture.swallowPrint;
const testSession = fixture.testSession;
const variants = @import("../../src/console/llm/variants.zig");
const renderVariant = variants.renderVariant;
const testing = std.testing;

test "plan undo/redo step the session's own history; running out of steps is a note (§2)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a); // 4x4
    defer session.deinit();

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);

    // Two REAL session edits to step back through: crop to 3x4, then rotate to 4x3.
    try testing.expect(applyOne(&session, .{ .crop = .{ .x1 = "1px", .y1 = "0px" } }, &edited, &steps));
    try testing.expect(applyOne(&session, .{ .rotate = .{ .dir = .right, .times = 1 } }, &edited, &steps));
    try testing.expectEqual(@as(usize, 4), session.current().width);
    try testing.expectEqual(@as(usize, 3), session.current().height);

    // One undo steps one HISTORY entry back (the rotate), one redo re-applies it.
    try testing.expect(applyOne(&session, .{ .undo = .{ .steps = 1 } }, &edited, &steps));
    try testing.expectEqual(@as(usize, 3), session.current().width);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "undone") != null);
    try testing.expect(applyOne(&session, .{ .redo = .{ .steps = 1 } }, &edited, &steps));
    try testing.expectEqual(@as(usize, 4), session.current().width);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "redone") != null);

    // Asking for more steps than exist takes what is there and notes the shortfall (§2).
    try testing.expect(applyOne(&session, .{ .undo = .{ .steps = 20 } }, &edited, &steps));
    try testing.expectEqual(@as(usize, 4), session.current().width); // back at the original
    try testing.expectEqual(@as(usize, 4), session.current().height);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "only 2 of 20 undo step(s) were available") != null);

    // At the original an undo has nothing to step — the /undo command's own message.
    try testing.expect(applyOne(&session, .{ .undo = .{ .steps = 1 } }, &edited, &steps));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "nothing to undo (at the original)") != null);
}

test "plan reset drops every pending edit back to the original (§2)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a); // 4x4
    defer session.deinit();

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    try testing.expect(applyOne(&session, .{ .crop = .{ .x1 = "1px", .y1 = "0px" } }, &edited, &steps));
    try testing.expect(applyOne(&session, .{ .rotate = .{ .dir = .left, .times = 1 } }, &edited, &steps));

    try testing.expect(applyOne(&session, .reset, &edited, &steps));
    try testing.expectEqual(@as(usize, 4), session.current().width);
    try testing.expectEqual(@as(usize, 4), session.current().height);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "reset to original") != null);
    // The history was dropped, not stepped: there is nothing to redo now.
    try testing.expect(applyOne(&session, .{ .redo = .{ .steps = 1 } }, &edited, &steps));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "nothing to redo (at the latest edit)") != null);
}

test "plan clear drops the working image through the /drop path (§10)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a);
    defer session.deinit();

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    try testing.expect(applyOne(&session, .clear, &edited, &steps));
    try testing.expect(!session.hasImage()); // the editor is empty, not a blank page
    try testing.expect(!edited); // a console-state change, not an image edit to sync
    // Cleared twice is /drop's own note — the plan carries on.
    try testing.expect(applyOne(&session, .clear, &edited, &steps));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "no image loaded") != null);
}

test "plan layout with an empty lines array removes every drawn line (§4)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a);
    defer session.deinit();
    try session.addLines("{\"lines\":[{\"points\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":1}],\"color\":\"#FF0000\"}]}");

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    try testing.expect(applyOne(&session, .{ .layout = .{ .lines_json = "[]" } }, &edited, &steps));
    try testing.expectEqualStrings("[]", session.state().lines_json);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "lines removed") != null);
    // Undoable, like the draw it removed.
    try testing.expect(session.undo());
    try testing.expect(std.mem.indexOf(u8, session.state().lines_json, "#FF0000") != null);
}

test "plan layout sets the drawn lines to exactly its own, one undoable entry (§2)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a);
    defer session.deinit();
    try session.addLines("{\"lines\":[{\"points\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":1}],\"color\":\"#FF0000\"}]}");

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    const blue = "[{\"points\":[{\"x\":2,\"y\":2},{\"x\":3,\"y\":3}],\"color\":\"#0000FF\"}]";
    try testing.expect(applyOne(&session, .{ .layout = .{ .lines_json = blue } }, &edited, &steps));
    const shown = session.state().lines_json;
    try testing.expect(std.mem.indexOf(u8, shown, "#0000FF") != null);
    try testing.expect(std.mem.indexOf(u8, shown, "#FF0000") == null); // replaced, never added to
    try testing.expect(session.undo());
    try testing.expect(std.mem.indexOf(u8, session.state().lines_json, "#FF0000") != null);
}

test "renderVariant: a filter op recolours the picture, not the lines already drawn" {
    const a = testing.allocator;
    var quiet: u8 = 0;
    logo.setSink(swallowPrint, &quiet);
    defer logo.clearSink();
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();

    // A red 16x12 picture with a green dot drawn on it, greyscaled by the variant: the
    // background turns grey, the annotation keeps its colour.
    var session = Session{ .gpa = a };
    defer session.deinit();
    const px = try a.alloc(u8, 16 * 12 * 4);
    core.fillRGBA(px, 16 * 12, .{ .r = 200, .g = 40, .b = 40, .a = 255 });
    try session.loadImage(.{ .width = 16, .height = 12, .pixels = px }, "test", true, .png, null);
    try session.addLines("{\"lines\":[{\"points\":[{\"x\":8,\"y\":6}],\"color\":\"#00FF00\",\"pointSize\":3,\"thickness\":2}]}");

    var actions = [_]llm.Action{.{ .filter = .{ .mode = .bw, .tint = "" } }};
    renderVariant(&session, io, .{ .label = "bw", .actions = &actions }, "bwtest", &.{});

    const dir = std.Io.Dir.cwd();
    defer dir.deleteFile(io, "variant-bwtest.png") catch {};
    const bytes = try dir.readFileAlloc(io, "variant-bwtest.png", a, .limited(1 << 20));
    defer a.free(bytes);
    var out = try image.decode(a, bytes);
    defer out.deinit(a);

    const dot = (6 * out.width + 8) * 4;
    try testing.expect(out.pixels[dot + 1] > 200); // green kept
    try testing.expect(out.pixels[dot] < 60 and out.pixels[dot + 2] < 60);
    const bg = (1 * out.width + 1) * 4;
    try testing.expectEqual(out.pixels[bg], out.pixels[bg + 1]);
    try testing.expectEqual(out.pixels[bg + 1], out.pixels[bg + 2]);
}

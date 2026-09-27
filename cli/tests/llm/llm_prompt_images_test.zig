//! §2.1 multi-image ops: `/upload` attachments, switching the working image, .stencil saves.
const std = @import("std");
const image = @import("../../src/media/image.zig");
const logo = @import("../../src/app/logo.zig");
const layout_mod = @import("../../src/media/layout.zig");
const project = @import("../../src/project.zig");
const Session = @import("../../src/console/session.zig").Session;
const fixture = @import("../../src/console/llm/fixture.zig");
const Capture = fixture.Capture;
const applyOne = fixture.applyOne;
const attachedSession = fixture.attachedSession;
const swallowPrint = fixture.swallowPrint;
const plan = @import("../../src/console/llm/plan.zig");
const applyPlanAction = plan.applyPlanAction;
const testing = std.testing;

test "plan image op adopts that upload as the working image and resets the frame (§2.1)" {
    const a = testing.allocator;
    var quiet: u8 = 0;
    logo.setSink(swallowPrint, &quiet);
    defer logo.clearSink();
    var session = try attachedSession(a);
    defer session.deinit();

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);

    // A crop first: its frame step must NOT survive the switch to a fresh picture.
    try testing.expect(applyOne(&session, .{ .crop = .{ .x1 = "1px", .y1 = "0px" } }, &edited, &steps));
    try testing.expectEqual(@as(usize, 1), steps.items.len);

    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var active: ?usize = null;
    try testing.expect(applyPlanAction(&session, threaded.io(), .{ .image = .{ .index = 2 } }, &edited, &steps, &active));
    try testing.expectEqual(@as(usize, 3), session.current().width); // the 3x5 second upload
    try testing.expectEqual(@as(usize, 5), session.current().height);
    try testing.expectEqual(@as(usize, 0), steps.items.len); // §1 accumulation reset
    try testing.expectEqual(@as(?usize, 2), active);

    // Switching back to the first upload is just as available (the list is not consumed).
    try testing.expect(applyPlanAction(&session, threaded.io(), .{ .image = .{ .index = 1 } }, &edited, &steps, &active));
    try testing.expectEqual(@as(usize, 6), session.current().width);
}

test "plan image op: an index the turn cannot satisfy costs the action, not the plan (§2.1)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try attachedSession(a);
    defer session.deinit();

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var active: ?usize = null;

    // true = keep going: the actions after it still run (contract §2.1).
    try testing.expect(applyPlanAction(&session, threaded.io(), .{ .image = .{ .index = 3 } }, &edited, &steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "skipped switching to attached image 3") != null);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "uploaded 2 image(s)") != null);
    try testing.expectEqual(@as(?usize, null), active);
    try testing.expectEqual(@as(usize, 4), session.current().width); // the working image stands
}

test "plan save op writes <name>.stencil, derived from the active upload, suffixed on collision (§2.1)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try attachedSession(a);
    defer session.deinit();

    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();
    defer dir.deleteFile(io, "dog.stencil") catch {};
    defer dir.deleteFile(io, "dog 2.stencil") catch {};
    defer dir.deleteFile(io, "kept.stencil") catch {};
    defer dir.deleteFile(io, "test.stencil") catch {};

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    var active: ?usize = null;

    // No attachment adopted yet → the working image's own label names the project.
    try testing.expect(applyPlanAction(&session, io, .{ .save = .{ .name = "", .path = "" } }, &edited, &steps, &active));
    try dir.access(io, "test.stencil", .{});

    // After an `image` op the ACTIVE upload names it — directory and extension dropped.
    try testing.expect(applyPlanAction(&session, io, .{ .image = .{ .index = 2 } }, &edited, &steps, &active));
    try testing.expect(applyPlanAction(&session, io, .{ .save = .{ .name = "", .path = "" } }, &edited, &steps, &active));
    try dir.access(io, "dog.stencil", .{});
    // A second save of the same image takes the next free " 2" name, never overwriting.
    try testing.expect(applyPlanAction(&session, io, .{ .save = .{ .name = "", .path = "" } }, &edited, &steps, &active));
    try dir.access(io, "dog 2.stencil", .{});

    // An explicit name wins, and a model-supplied path can never escape the cwd.
    try testing.expect(applyPlanAction(&session, io, .{ .save = .{ .name = "../../kept.png", .path = "" } }, &edited, &steps, &active));
    try dir.access(io, "kept.stencil", .{});

    // The saved bundle is a real project: it re-opens with the switched image's size.
    var reopened = Session{ .gpa = a };
    defer reopened.deinit();
    var proj = try project.loadInto(&reopened, io, "dog.stencil");
    proj.deinit();
    try testing.expectEqual(@as(usize, 3), reopened.current().width);
    try testing.expectEqual(@as(usize, 5), reopened.current().height);
}

test "plan save op with nothing loaded is skipped with a note (§2.1)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = Session{ .gpa = a }; // no image at all
    defer session.deinit();

    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    var active: ?usize = null;
    try testing.expect(applyPlanAction(&session, threaded.io(), .{ .save = .{ .name = "x", .path = "" } }, &edited, &steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "skipped save — no working image") != null);
}

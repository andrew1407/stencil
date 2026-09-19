//! §3/§7 `/prompt` turn behaviour: what rides with a turn, and where a plan stops.
const std = @import("std");
const logo = @import("../src/logo.zig");
const llm = @import("../src/llm.zig");
const layout_mod = @import("../src/layout.zig");
const Session = @import("../src/console/session.zig").Session;
const attach = @import("../src/console/llm/attach.zig");
const promptImageB64 = attach.promptImageB64;
const edgeMapB64 = attach.edgeMapB64;
const fixture = @import("../src/console/llm/fixture.zig");
const Capture = fixture.Capture;
const applyOne = fixture.applyOne;
const swallowPrint = fixture.swallowPrint;
const testSession = fixture.testSession;
const plan = @import("../src/console/llm/plan.zig");
const runPlan = plan.runPlan;
const testing = std.testing;

test "edge map: rides beside the working snapshot; over its own cap only it drops (§7)" {
    const a = testing.allocator;
    var quiet: u8 = 0;
    logo.setSink(swallowPrint, &quiet);
    defer logo.clearSink();
    var session = try testSession(a);
    defer session.deinit();

    const working = (try promptImageB64(&session)).?; // session-owned (cached)
    const edge = (try edgeMapB64(&session, llm.max_image_bytes)).?;
    defer a.free(edge);
    // A distinct second image: the contour render, not a copy of the snapshot.
    try testing.expect(!std.mem.eql(u8, working, edge));

    // A cap only the edge map exceeds drops just it — the snapshot still rides.
    try testing.expect((try edgeMapB64(&session, 8)) == null);
    try testing.expect((try promptImageB64(&session)) != null);
}

test "runPlan: a layout turn ends at its plan — one model round, nothing after it (§3.0)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a); // 4x4
    defer session.deinit();

    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const raw = "{\"version\":1,\"reply\":\"traced it\",\"actions\":[{\"op\":\"layout\",\"lines\":[" ++
        "{\"points\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":1}],\"color\":\"#FF0000\"}," ++
        "{\"points\":[{\"x\":3,\"y\":3},{\"x\":2,\"y\":2}],\"color\":\"#0000FF\"}]}]}";
    // False = no §7 continuation, so doPrompt's round loop stops: the turn cost ONE request.
    try testing.expect(!try runPlan(&session, threaded.io(), raw, "outline the shapes"));

    // The whole transcript is the reply plus the draw acknowledgement: every request this module makes
    // announces itself or its failure through the same sink, so the exact match IS the round count.
    try testing.expectEqualStrings("traced it\ndrawn -> 4x4 px · A4 21×29.7cm  [2/2]\n", cap.text());
    inline for (.{ "correction", "self-check", "keeping the lines as planned", "sharpen" }) |phrase| {
        try testing.expect(std.mem.indexOf(u8, cap.text(), phrase) == null);
    }

    // The model's traced lines ARE the result: kept verbatim, in plan order.
    const after = session.state().lines_json;
    try testing.expect(std.mem.indexOf(u8, after, "{\"x\":0,\"y\":0}") != null);
    try testing.expect(std.mem.indexOf(u8, after, "{\"x\":3,\"y\":3}") != null);
    try testing.expect(std.mem.indexOf(u8, after, "#FF0000") != null);
    try testing.expect(std.mem.indexOf(u8, after, "#0000FF") != null);
}

test "runPlan: a stray line off the subject is kept as planned — no follow-up round (§3.0)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a); // 4x4
    defer session.deinit();

    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    // Two lines whose boxes are disjoint — the shape the withdrawn suspect check chased.
    const raw = "{\"version\":1,\"reply\":\"done\",\"actions\":[{\"op\":\"layout\",\"lines\":[" ++
        "{\"points\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":1}]}," ++
        "{\"points\":[{\"x\":3,\"y\":3},{\"x\":4,\"y\":4}]}]}]}";
    try testing.expect(!try runPlan(&session, threaded.io(), raw, "outline the cat"));
    try testing.expectEqualStrings("done\ndrawn -> 4x4 px · A4 21×29.7cm  [2/2]\n", cap.text());
}

test "plan layout after crop/rotate is re-mapped into the current frame (§1)" {
    const a = testing.allocator;
    var quiet: u8 = 0;
    logo.setSink(swallowPrint, &quiet);
    defer logo.clearSink();

    // An 8x8 white session image.
    var session = Session{ .gpa = a };
    defer session.deinit();
    const px = try a.alloc(u8, 8 * 8 * 4);
    @memset(px, 255);
    try session.loadImage(.{ .width = 8, .height = 8, .pixels = px }, "test", true, .png, null);

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);

    // Crop x1=4px (y kept full so the single-axis aspect derivation stays out of play)
    // → view 4x8 with origin (4,0).
    try testing.expect(applyOne(&session, .{ .crop = .{ .x1 = "4px", .y1 = "0px" } }, &edited, &steps));
    try testing.expectEqual(@as(usize, 4), session.current().width);
    // Rotate right once → view 8x4.
    try testing.expect(applyOne(&session, .{ .rotate = .{ .dir = .right, .times = 1 } }, &edited, &steps));
    try testing.expectEqual(@as(usize, 2), steps.items.len);

    // A layout in the SNAPSHOT frame: (6,2) → crop → (2,2) → rotate(4x8 CW) → (6,2).
    try testing.expect(applyOne(&session, .{ .layout = .{
        .lines_json = "[{\"points\":[{\"x\":6,\"y\":2},{\"x\":5,\"y\":3}],\"color\":\"#FF0000\"}]",
    } }, &edited, &steps));

    const after = session.state().lines_json;
    // (6,2) → (6,2) here (crop then rotate compose); (5,3) → (1,3) → (8−3,1) = (5,1).
    try testing.expect(std.mem.indexOf(u8, after, "{\"x\":6,\"y\":2}") != null);
    try testing.expect(std.mem.indexOf(u8, after, "{\"x\":5,\"y\":1}") != null);
}

test "plan layout with no earlier crop/rotate is clamped but not re-mapped (§1)" {
    const a = testing.allocator;
    var quiet: u8 = 0;
    logo.setSink(swallowPrint, &quiet);
    defer logo.clearSink();
    var session = try testSession(a); // 4x4
    defer session.deinit();

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    try testing.expect(applyOne(&session, .{ .layout = .{
        .lines_json = "[{\"points\":[{\"x\":2,\"y\":1},{\"x\":99,\"y\":-7}]}]",
    } }, &edited, &steps));

    const after = session.state().lines_json;
    try testing.expect(std.mem.indexOf(u8, after, "{\"x\":2,\"y\":1}") != null); // untouched
    try testing.expect(std.mem.indexOf(u8, after, "{\"x\":4,\"y\":0}") != null); // clamped into 4x4
}


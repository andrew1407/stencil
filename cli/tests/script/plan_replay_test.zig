//! `--script-plan` replayed through a real op-plan executor — the console's `runPlan`, one plan
//! at a time as an adapter feeds them — leaves the pixels `--script` saves: an `@undo` removes
//! the edits the rewind removes, a shape after a `@crop` lands where the one-shot draws it, the
//! lines before a crop scale or clear by the one rule, a crop in any unit cuts the same window,
//! and a line keeps its point colour.
const std = @import("std");
const testing = std.testing;

const replay = @import("replay.zig");

const head = "@source ../common/samples/sample.png:\n"; // 16x12, album
const doc = "stencil_replay_doc.json";

fn expectSame(io: std.Io, ops: []const u8) !void {
    return replay.expectSame(io, head, ops);
}

const green = "@use line #00ff00 2px; @line (2,3) (14,9)";

test "an @undo in a plan removes what the one-shot's rewind removes" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    try expectSame(io, "@filter aqua; @rect (1,1) (9,9); @undo"); // the filter stays, the rect goes
    try expectSame(io, "@rect (1,1) (9,9); @filter aqua; @undo"); // the rect stays, the filter goes
    try expectSame(io, green ++ "; @line (0,0) (15,11); @filter bw; @undo 2"); // a batch cut in two
    try expectSame(io, green ++ "; @crop 25%; @line (1,1) (6,4); @undo 2"); // past a crop, then a replay
}

test "a shape after a @crop lands where the one-shot draws it, and the lines before it scale" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    try expectSame(io, green ++ "; @crop 25%");
    // In the view: an executor clamps a layout point into it (§1), where --script does not.
    try expectSame(io, "@crop 25%; @use line #00ff00 2px; @line (1,1) (7,5)");
    try expectSame(io, green ++ "; @crop 2px 1px; @line (0,0) (6,5); @crop 25%; @rect (1,1) (4,3)");
    try expectSame(io, green ++ "; @crop x1=0 x2=50% y1=0 y2=100%; @line (1,1) (6,10)"); // cleared, then drawn
}

test "a replacing @layout clears the lines drawn, and an @undo of it brings them back" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();
    try dir.writeFile(io, .{ .sub_path = doc, .data = "{\"lines\":[{\"points\":[{\"x\":0,\"y\":0}," ++
        "{\"x\":15,\"y\":11}],\"color\":\"#0000ff\",\"thickness\":2,\"pointSize\":0,\"style\":\"solid\"}]}" });
    defer dir.deleteFile(io, doc) catch {};
    try expectSame(io, green ++ "; @layout " ++ doc ++ " replace");
    try expectSame(io, green ++ "; @layout " ++ doc ++ " replace; @undo");
    try expectSame(io, green ++ "; @layout " ++ doc);
    try expectSame(io, green ++ "; @layout " ++ doc ++ " replace; @line (1,2) (5,6)");
    try expectSame(io, green ++ "; @layout " ++ doc ++ "; @line (1,1) (6,4); @undo");
    try expectSame(io, "@layout " ++ doc ++ "; @crop 25%; @line (1,1) (6,4)"); // the doc's line scaled, then carried
}

test "every layout carries the lines already shown, as a replacing executor needs" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    try expectSame(io, green ++ "; @filter bw; @line (0,0) (15,11)");
    try expectSame(io, green ++ "; @crop 25%; @line (1,1) (6,4)");
    try expectSame(io, green ++ "; @crop 25%; @undo"); // the crop's scaling undone with it
    try expectSame(io, green ++ "; @crop x1=0 x2=50%; @line (1,1) (6,10); @filter sepia; @rect (2,2) (5,5)");
    try expectSame(io, green ++ "; @line (0,0) (15,11); @filter bw; @line (3,3) (9,9); @undo 3");
}

test "a crop in cm, mm or in, or on one axis, cuts what the one-shot cuts, whatever the page" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    try expectSame(io, green ++ "; @crop x1=1mm x2=-2mm y1=0.5mm y2=-0.05in");
    try expectSame(io, green ++ "; @crop x1=25% x2=-25%"); // the height follows the image's own aspect
    try expectSame(io, green ++ "; @crop x1=25% x2=-25% album");
    try expectSame(io, green ++ "; @crop y1=2px y2=-0.1cm");
}

test "a line's point colour reaches the executor" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    try expectSame(io, "@use line #00ff00 2px point #ff0000 5; @line (2,3) (14,9)");
    try expectSame(io, "@use line #00ff00 2px point red 5; @rect (2,3) (14,9)");
}

//! A `--script` run keeps its marks as vectors over the picture, as every editor keeps its
//! lines: a `@crop` scales them by the width ratio or clears them on an album/portrait flip, a
//! `@save` draws them on a copy, an `@undo` replays them through a crop, and `--script-plan`
//! clears the drawn lines before a replacing `@layout`. The console's `/script` is the reference.
const std = @import("std");
const testing = std.testing;

const console = @import("../../src/console.zig");
const logo = @import("../../src/app/logo.zig");
const plan = @import("../../src/script/plan.zig");
const run = @import("../../src/script/run.zig");
const scriptCore = @import("../../src/script/core.zig");
const pixelsOf = @import("pixels.zig").pixelsOf;

const head = "@source ../common/samples/sample.png:\n"; // 16x12, album
const stc = "stencil_marks_run.stc";

/// Runs `body` as the one block over the fixture; its `@save`s name their own files.
fn runBlock(io: std.Io, body: []const u8) !void {
    const dir = std.Io.Dir.cwd();
    var text: std.ArrayList(u8) = .empty;
    defer text.deinit(testing.allocator);
    try text.print(testing.allocator, head ++ "{s}\n", .{body});
    try dir.writeFile(io, .{ .sub_path = stc, .data = text.items });
    defer dir.deleteFile(io, stc) catch {};
    try run.run(testing.allocator, io, .{}, stc);
}

fn quiet(_: *anyopaque, _: []const u8) void {}

/// What the console shows after `/script ops` on the fixture: the reference every editor matches.
fn consoleView(io: std.Io, ops: []const u8) ![]u8 {
    var unused: u8 = 0;
    logo.setSink(quiet, &unused);
    defer logo.clearSink();
    var session: console.Session = .{ .gpa = testing.allocator };
    defer session.deinit();
    _ = try console.handle(&session, io, "/upload ../common/samples/sample.png");
    var line: std.ArrayList(u8) = .empty;
    defer line.deinit(testing.allocator);
    try line.print(testing.allocator, "/script {s}", .{ops});
    _ = try console.handle(&session, io, line.items);
    try testing.expectEqual(@as(usize, 2), session.stateCount()); // the image, then the whole run
    return testing.allocator.dupe(u8, session.current().pixels);
}

/// Every output a case wrote, removed however it ends.
const Outputs = struct {
    io: std.Io,
    names: []const []const u8,

    fn deinit(self: Outputs) void {
        for (self.names) |n| std.Io.Dir.cwd().deleteFile(self.io, n) catch {};
    }

    fn pixels(self: Outputs, k: usize) ![]u8 {
        return pixelsOf(testing.allocator, self.io, self.names[k]);
    }
};

const green = "@use line #00ff00 2px; @line (2,3) (14,9)";

test "a line then a crop: the line scales by the width ratio, exactly as the console's /script draws it" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const out: Outputs = .{ .io = threaded.io(), .names = &.{ "stencil_marks_scaled.png", "stencil_marks_direct.png" } };
    defer out.deinit();

    // 25% insets take 16x12 to 8x6: scale 0.5, so (2,3) (14,9) lands on (1,1.5) (7,4.5).
    try runBlock(out.io, green ++ "; @crop 25%; @save stencil_marks_scaled.png");
    try runBlock(out.io, "@crop 25%; @use line #00ff00 2px; @line (1,1.5) (7,4.5); @save stencil_marks_direct.png");
    const scaled = try out.pixels(0);
    defer testing.allocator.free(scaled);
    const direct = try out.pixels(1);
    defer testing.allocator.free(direct);
    try testing.expectEqual(@as(usize, 8 * 6 * 4), scaled.len);
    try testing.expectEqualSlices(u8, direct, scaled);

    const view = try consoleView(out.io, green ++ "; @crop 25%");
    defer testing.allocator.free(view);
    try testing.expectEqualSlices(u8, view, scaled);
}

test "a crop that flips album to portrait clears the lines" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const out: Outputs = .{ .io = threaded.io(), .names = &.{ "stencil_marks_flip.png", "stencil_marks_bare.png" } };
    defer out.deinit();

    try runBlock(out.io, green ++ "; @crop x1=0 x2=50% y1=0 y2=100%; @save stencil_marks_flip.png");
    try runBlock(out.io, "@crop x1=0 x2=50% y1=0 y2=100%; @save stencil_marks_bare.png");
    const flipped = try out.pixels(0);
    defer testing.allocator.free(flipped);
    const bare = try out.pixels(1);
    defer testing.allocator.free(bare);
    try testing.expectEqual(@as(usize, 8 * 12 * 4), flipped.len);
    try testing.expectEqualSlices(u8, bare, flipped);
}

test "a save draws the marks on a copy: a later @filter leaves them unfiltered in the next save" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const out: Outputs = .{ .io = threaded.io(), .names = &.{ "stencil_marks_first.png", "stencil_marks_second.png" } };
    defer out.deinit();

    const red = "@use line #ff0000 3px; @line (0,6) (15,6)";
    try runBlock(out.io, red ++ "; @save stencil_marks_first.png; @filter bw; @save stencil_marks_second.png");
    const first = try out.pixels(0);
    defer testing.allocator.free(first);
    const second = try out.pixels(1);
    defer testing.allocator.free(second);

    const on_line = (6 * 16 + 8) * 4;
    const off_line = (0 * 16 + 8) * 4;
    try testing.expectEqualSlices(u8, first[on_line..][0..4], second[on_line..][0..4]);
    try testing.expect(second[on_line] > 200 and second[on_line + 1] < 60); // still red
    try testing.expect(second[off_line] == second[off_line + 1] and second[off_line + 1] == second[off_line + 2]);

    const view = try consoleView(out.io, red ++ "; @filter bw");
    defer testing.allocator.free(view);
    try testing.expectEqualSlices(u8, view, second);
}

test "an @undo replays the marks through the crop, and past it brings them back unscaled" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const out: Outputs = .{ .io = threaded.io(), .names = &.{ "stencil_marks_kept.png", "stencil_marks_undone.png", "stencil_marks_line.png" } };
    defer out.deinit();

    const ops = green ++ "; @crop 25%; @line (0,0) (7,5); @undo";
    try runBlock(out.io, ops ++ "; @save stencil_marks_kept.png; @undo; @save stencil_marks_undone.png");
    try runBlock(out.io, green ++ "; @save stencil_marks_line.png");
    const kept = try out.pixels(0);
    defer testing.allocator.free(kept);
    const view = try consoleView(out.io, ops);
    defer testing.allocator.free(view);
    try testing.expectEqualSlices(u8, view, kept); // the rewind scaled the line again

    const undone = try out.pixels(1);
    defer testing.allocator.free(undone);
    const line_only = try out.pixels(2);
    defer testing.allocator.free(line_only);
    try testing.expectEqualSlices(u8, line_only, undone);
}

test "a replacing @layout drops every mark, and --script-plan lands the document's lines alone" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const doc = "stencil_marks_doc.json";
    const out: Outputs = .{ .io = io, .names = &.{ "stencil_marks_replaced.png", "stencil_marks_doc_only.png", doc, "stencil_marks_saved.png" } };
    defer out.deinit();
    try std.Io.Dir.cwd().writeFile(io, .{ .sub_path = doc, .data = "{\"lines\":[{\"points\":[{\"x\":0,\"y\":0}," ++
        "{\"x\":15,\"y\":11}],\"color\":\"#0000ff\",\"thickness\":2,\"pointSize\":0,\"style\":\"solid\"}]}" });

    const body = green ++ "; @save stencil_marks_saved.png; @layout " ++ doc ++ " replace; @save stencil_marks_replaced.png";
    try runBlock(io, body);
    try runBlock(io, "@layout " ++ doc ++ "; @save stencil_marks_doc_only.png");
    const replaced = try out.pixels(0);
    defer testing.allocator.free(replaced);
    const doc_only = try out.pixels(1);
    defer testing.allocator.free(doc_only);
    try testing.expectEqualSlices(u8, doc_only, replaced); // the line saved before is gone too

    var s = try scriptCore.Script.parse(head ++ body ++ "\n");
    defer s.deinit();
    var env: std.Io.Writer.Allocating = .init(testing.allocator);
    defer env.deinit();
    try plan.writeEnvelope(testing.allocator, io, &env.writer, s, .{}, "m.stc");
    var parsed = try std.json.parseFromSlice(std.json.Value, testing.allocator, env.written(), .{});
    defer parsed.deinit();
    const actions = parsed.value.object.get("blocks").?.array.items[0]
        .object.get("plans").?.array.items[0].object.get("actions").?.array.items;
    var ops: std.ArrayList(u8) = .empty;
    defer ops.deinit(testing.allocator);
    for (actions) |v| {
        const o = v.object;
        try ops.appendSlice(testing.allocator, o.get("op").?.string);
        if (o.get("lines")) |l| try ops.print(testing.allocator, "[{d}]", .{l.array.items.len});
        try ops.append(testing.allocator, ' ');
    }
    try testing.expectEqualStrings("openFile layout[1] save layout[1] save ", ops.items);
}

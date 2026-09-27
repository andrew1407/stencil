//! The console's `/script` as one history step: many shapes land as one state, `@undo` inside
//! the script walks its own edits back one by one, `/undo` takes the whole run back, a replacing
//! `@layout` drops the lines placed so far, and an op that cannot resolve stops the run with
//! nothing applied (stc-contract §10).
const std = @import("std");
const console = @import("../../src/console.zig");
const logo = @import("../../src/app/logo.zig");
const testing = std.testing;
const sample = @embedFile("../fixtures/sample.png");

const Cap = struct {
    buf: std.ArrayList(u8) = .empty,
    fn sink(ctx: *anyopaque, bytes: []const u8) void {
        const self: *Cap = @ptrCast(@alignCast(ctx));
        self.buf.appendSlice(testing.allocator, bytes) catch {};
    }
};

/// A session with the 16x12 fixture loaded, its output captured.
const Loaded = struct {
    threaded: std.Io.Threaded,
    session: console.Session,
    cap: Cap = .{},
    path: []const u8,

    fn init(self: *Loaded, path: []const u8) !void {
        self.* = .{ .threaded = std.Io.Threaded.init(testing.allocator, .{}), .session = .{ .gpa = testing.allocator }, .path = path };
        try std.Io.Dir.cwd().writeFile(self.io(), .{ .sub_path = path, .data = sample });
        logo.setSink(Cap.sink, &self.cap);
        var line: [128]u8 = undefined;
        _ = try console.handle(&self.session, self.io(), try std.fmt.bufPrint(&line, "/upload {s}", .{path}));
    }

    fn io(self: *Loaded) std.Io {
        return self.threaded.io();
    }

    fn deinit(self: *Loaded) void {
        logo.clearSink();
        std.Io.Dir.cwd().deleteFile(self.io(), self.path) catch {};
        self.cap.buf.deinit(testing.allocator);
        self.session.deinit();
        self.threaded.deinit();
    }
};

test "/script: a run of shapes is one state, and its @undo steps back one shape" {
    var l: Loaded = undefined;
    try l.init("stencil_script_runs_shapes.png");
    defer l.deinit();
    _ = try console.handle(&l.session, l.io(), "/script @line (0,0) (4,4); @line (1,1) (5,5); @rect (2,2) (6,6); @undo; @crop 25%; @line (0,0) (1,1)");
    try testing.expectEqual(@as(usize, 2), l.session.stateCount());
    const lines = l.session.state().lines();
    try testing.expectEqual(@as(usize, 3), std.mem.count(u8, lines, "\"points\"")); // the @rect was undone
    try testing.expect(std.mem.indexOf(u8, lines, "\"locked\":true") == null);
    try testing.expectEqual(@as(usize, 8), l.session.current().width);
    _ = try console.handle(&l.session, l.io(), "/undo"); // the whole run, in one step
    try testing.expectEqual(@as(usize, 16), l.session.current().width);
    try testing.expectEqualStrings("[]", l.session.state().lines());
}

test "/script: a run past the history cap is still one step, and the edits before it stay" {
    var l: Loaded = undefined;
    try l.init("stencil_script_runs_cap.png");
    defer l.deinit();
    _ = try console.handle(&l.session, l.io(), "/script @filter sepia");
    const twice = "@filter bw; @filter sepia; ";
    _ = try console.handle(&l.session, l.io(), "/script " ++ twice ** 40 ++ "@filter bw");
    try testing.expectEqual(@as(usize, 3), l.session.stateCount());
    try testing.expectEqualStrings("bw", l.session.state().filter_mode);
    _ = try console.handle(&l.session, l.io(), "/undo"); // the whole run, in one step
    try testing.expectEqualStrings("sepia", l.session.state().filter_mode);
    _ = try console.handle(&l.session, l.io(), "/undo");
    try testing.expectEqual(@as(usize, 0), l.session.cursor);
}

test "/script: an op that cannot resolve is reported, and the run takes nothing" {
    var l: Loaded = undefined;
    try l.init("stencil_script_runs_unres.png");
    defer l.deinit();
    try testing.expect(!try console.handle(&l.session, l.io(), "/script @crop 25%; @layout stencil_script_runs_absent.json"));
    try testing.expect(std.mem.indexOf(u8, l.cap.buf.items, "error: the script stopped at op 2") != null);
    try testing.expectEqual(@as(usize, 1), l.session.stateCount());
    try testing.expectEqual(@as(usize, 16), l.session.current().width); // the crop before it was taken back
}

test "/script: a replacing @layout drops the lines placed so far, and its @undo brings them back" {
    var l: Loaded = undefined;
    try l.init("stencil_script_runs_replace.png");
    defer l.deinit();
    const doc = "stencil_script_runs_replace.json";
    try std.Io.Dir.cwd().writeFile(l.io(), .{ .sub_path = doc, .data = "{\"lines\":[{\"points\":[{\"x\":1,\"y\":1},{\"x\":9,\"y\":9}],\"color\":\"#0000ff\"}]}" });
    defer std.Io.Dir.cwd().deleteFile(l.io(), doc) catch {};

    _ = try console.handle(&l.session, l.io(), "/script @line (0,0) (4,4); @rect (2,2) (6,6); @layout " ++ doc ++ " replace");
    const lines = l.session.state().lines();
    try testing.expectEqual(@as(usize, 1), std.mem.count(u8, lines, "\"points\"")); // the doc's alone
    try testing.expect(std.mem.indexOf(u8, lines, "#0000ff") != null);

    _ = try console.handle(&l.session, l.io(), "/undo");
    _ = try console.handle(&l.session, l.io(), "/script @line (0,0) (4,4); @layout " ++ doc ++ " replace; @undo");
    try testing.expectEqual(@as(usize, 1), std.mem.count(u8, l.session.state().lines(), "\"points\""));
    try testing.expect(std.mem.indexOf(u8, l.session.state().lines(), "#0000ff") == null); // the @line again
}

//! A console script run as a log of its edits, replayed into the session: consecutive shapes
//! land as ONE state (N shapes cost one snapshot and one rasterize, not N copies of a growing
//! drawing), and an `@undo` pops the log and replays what survives from where the run began.
const std = @import("std");
const core = @import("../../core.zig");
const Session = @import("../session.zig").Session;
const scriptCore = @import("../../script/core.zig");

/// One recorded edit, its data owned: a shape as its layout line JSON, a layout as its document
/// and whether it replaces the lines drawn, as the editors' and `--script`'s replace does.
pub const Entry = union(enum) {
    crop: core.Rect,
    filter: struct { mode: []u8, tint: []u8 },
    shape: []u8,
    layout: struct { doc: []u8, replace: bool },

    fn deinit(self: Entry, gpa: std.mem.Allocator) void {
        switch (self) {
            .crop => {},
            .filter => |f| {
                gpa.free(f.mode);
                gpa.free(f.tint);
            },
            .shape => |b| gpa.free(b),
            .layout => |l| gpa.free(l.doc),
        }
    }
};

/// Runs on a session whose `run_floor` is set: the run holds one state above it.
pub const Log = struct {
    gpa: std.mem.Allocator,
    entries: std.ArrayList(Entry) = .empty,
    batch: Batch = .{},

    pub fn deinit(self: *Log) void {
        for (self.entries.items) |e| e.deinit(self.gpa);
        self.entries.deinit(self.gpa);
        self.batch.deinit(self.gpa);
    }

    /// Record `e` (taking ownership) and apply it.
    pub fn record(self: *Log, session: *Session, e: Entry) !void {
        self.entries.append(self.gpa, e) catch |err| {
            e.deinit(self.gpa);
            return err;
        };
        try self.apply(session, e);
    }

    pub fn shape(self: *Log, session: *Session, line: core.LineDraw) !void {
        try self.record(session, .{ .shape = try std.json.Stringify.valueAlloc(self.gpa, ShapeJson.of(line), .{}) });
    }

    /// `@undo n`: the newest `n` edits go, and what survives is replayed from where the run began.
    pub fn undo(self: *Log, session: *Session, n: usize) !void {
        const keep = self.entries.items.len -| n;
        for (self.entries.items[keep..]) |e| e.deinit(self.gpa);
        self.entries.shrinkRetainingCapacity(keep);
        self.batch.lines.clearRetainingCapacity();
        self.batch.ends.clearRetainingCapacity();
        session.cursor = session.run_floor.?;
        session.dropAfterCursor();
        session.rebuild() catch {};
        for (self.entries.items) |e| try self.apply(session, e);
    }

    /// Land what is still waiting; the run is done.
    pub fn finish(self: *Log, session: *Session) !void {
        const view = session.current();
        try self.batch.flush(session, view.width, view.height);
    }

    fn apply(self: *Log, session: *Session, e: Entry) !void {
        if (e == .shape) return self.batch.add(self.gpa, e.shape);
        try self.finish(session);
        switch (e) {
            .crop => |r| try session.applyCrop(r),
            .filter => |f| try session.setFilter(f.mode, f.tint),
            .layout => |l| if (l.replace) try session.setLines(l.doc) else try session.addLines(l.doc),
            .shape => unreachable,
        }
    }
};

/// The shapes waiting for the run's next other edit, as one layout document's `lines`.
const Batch = struct {
    lines: std.ArrayList(u8) = .empty, // the shapes' JSON objects, comma-joined
    ends: std.ArrayList(usize) = .empty, // where each one's object ends in `lines`

    pub fn deinit(self: *Batch, gpa: std.mem.Allocator) void {
        self.lines.deinit(gpa);
        self.ends.deinit(gpa);
    }

    pub fn add(self: *Batch, gpa: std.mem.Allocator, obj: []const u8) !void {
        if (self.ends.items.len != 0) try self.lines.append(gpa, ',');
        try self.lines.appendSlice(gpa, obj);
        try self.ends.append(gpa, self.lines.items.len);
    }

    /// Land the waiting shapes as one layout document on a `w`×`h` view.
    pub fn flush(self: *Batch, session: *Session, w: usize, h: usize) !void {
        if (self.ends.items.len == 0) return;
        const doc = try std.fmt.allocPrint(session.gpa, "{{\"imageWidth\":{d},\"imageHeight\":{d},\"lines\":[{s}]}}", .{ w, h, self.lines.items });
        defer session.gpa.free(doc);
        try session.addLines(doc);
        self.lines.clearRetainingCapacity();
        self.ends.clearRetainingCapacity();
    }
};

/// One shape in the browser's layout line shape; std.json writes it, so no string can break out.
const ShapeJson = struct {
    color: []const u8,
    style: []const u8,
    fillColor: []const u8,
    pointColor: []const u8,
    thickness: f64,
    pointSize: f64,
    locked: bool,
    points: []const Pt,

    const Pt = struct { x: f64, y: f64 };
    threadlocal var pts: [@typeInfo(scriptCore.ResolveBuf).array.len / 2]Pt = undefined;

    fn of(line: core.LineDraw) ShapeJson {
        const n = @min(line.points.len / 2, pts.len);
        for (pts[0..n], 0..) |*p, k| p.* = .{ .x = line.points[2 * k], .y = line.points[2 * k + 1] };
        return .{ .color = line.color, .style = line.style, .fillColor = line.fill_color, .pointColor = line.point_color, .thickness = line.thickness, .pointSize = line.point_size, .locked = line.locked, .points = pts[0..n] };
    }
};

const testing = std.testing;

test "a shape is written by std.json, so a colour holding quotes stays one string" {
    const a = testing.allocator;
    const pts = [_]f64{ 1, 2, 3, 4 };
    const line = core.LineDraw{ .points = &pts, .color = "red\",\"x\":\"", .style = "solid", .fill_color = "", .point_color = "", .thickness = 2, .point_size = 0, .locked = false };
    const obj = try std.json.Stringify.valueAlloc(a, ShapeJson.of(line), .{});
    defer a.free(obj);
    var parsed = try std.json.parseFromSlice(std.json.Value, a, obj, .{});
    defer parsed.deinit();
    try testing.expectEqualStrings("red\",\"x\":\"", parsed.value.object.get("color").?.string);
    try testing.expectEqual(@as(usize, 2), parsed.value.object.get("points").?.array.items.len);
}

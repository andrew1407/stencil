//! One lowered op -> the typed edit it names. Every walker of the op stream — the runner, the
//! console, the planner — decodes here, so a crop, a shape and a filter are read out of the
//! core in exactly one place; what a surface then DOES with the edit is its own.
const std = @import("std");

const core = @import("../core.zig");
const scriptCore = @import("../scriptCore.zig");

pub const Filter = struct {
    mode: []const u8,
    tint: []const u8,

    /// What a filter call takes: "custom" means the tint itself is the mode.
    pub fn effective(self: Filter) []const u8 {
        return if (std.mem.eql(u8, self.mode, "custom")) self.tint else self.mode;
    }
};

pub const Layout = struct { src: []const u8, mode: []const u8, kind: scriptCore.SourceKind };

/// The edit an op names, every length already in pixels. A `shape`'s points live in the
/// caller's `ResolveBuf` — copy them before the next decode.
pub const Edit = union(enum) {
    crop: core.Rect,
    shape: core.LineDraw,
    filter: Filter,
    layout: Layout,
    save: []const u8,
    frame: u32,
    /// `@undo` / `@redo`: how far to move, never less than one.
    steps: usize,
    /// `@open`, the block header — nothing to apply.
    none,
};

fn resolved(script: scriptCore.Script, i: u32, w: f64, h: f64, buf: *scriptCore.ResolveBuf) ?[]f64 {
    const r = script.resolve(i, w, h, scriptCore.PX_PER_CM, scriptCore.PX_PER_CM, buf) catch return null;
    return if (r.len < 4) null else r;
}

fn px(v: f64) i32 {
    return @intFromFloat(@round(v));
}

/// Decodes op `i` against a `w`×`h` image. Null only when a length resolved to nothing — the
/// one failure a caller has to decide about (report and stop, or skip the op).
pub fn decode(
    script: scriptCore.Script,
    i: u32,
    kind: scriptCore.OpKind,
    w: f64,
    h: f64,
    buf: *scriptCore.ResolveBuf,
) ?Edit {
    switch (kind) {
        .open => return .none,
        .crop => {
            const r = resolved(script, i, w, h, buf) orelse return null;
            return .{ .crop = .{ .x = px(r[0]), .y = px(r[1]), .w = px(r[2]), .h = px(r[3]) } };
        },
        .line, .rect => {
            const r = resolved(script, i, w, h, buf) orelse return null;
            return .{ .shape = .{
                .points = r[0 .. r.len - 2],
                .color = script.opStr(i, 0),
                .style = script.opStr(i, 1),
                .fill_color = script.opStr(i, 2),
                .point_color = script.opStr(i, 3),
                .thickness = r[r.len - 2],
                .point_size = r[r.len - 1],
                .locked = kind == .rect,
            } };
        },
        .filter => return .{ .filter = .{ .mode = script.opStr(i, 0), .tint = script.opStr(i, 1) } },
        .layout => return .{
            .layout = .{
                .src = script.opStr(i, 0),
                .mode = script.opStr(i, 1),
                // The core carries the layout source's own classification as its first number.
                .kind = @enumFromInt(@as(c_int, @intFromFloat(script.opNum(i, 0) orelse 1))),
            },
        },
        .save => return .{ .save = script.opStr(i, 0) },
        .frame => return .{ .frame = @intFromFloat(@max(0, script.opNum(i, 0) orelse 0)) },
        .undo, .redo => return .{ .steps = @intFromFloat(@max(1, script.opNum(i, 0) orelse 1)) },
    }
}

const testing = std.testing;

test "a crop decodes to rounded pixels of the size it is given" {
    var s = try scriptCore.Script.parse("@source a.png:\n  @crop 10%\n");
    defer s.deinit();
    var buf: scriptCore.ResolveBuf = undefined;
    const r = decode(s, 1, .crop, 200, 100, &buf).?.crop;
    try testing.expectEqual(@as(i32, 20), r.x);
    try testing.expectEqual(@as(i32, 160), r.w);
}

test "a shape decodes to a LineDraw the core can rasterize as it stands" {
    var s = try scriptCore.Script.parse("@source a.png:\n  @rect (1,1) (5,5)\n");
    defer s.deinit();
    var buf: scriptCore.ResolveBuf = undefined;
    const line = decode(s, 1, .rect, 40, 40, &buf).?.shape;
    try testing.expect(line.locked);
    try testing.expectEqual(@as(usize, 8), line.points.len); // a rect closes its four corners
    try testing.expectEqualStrings("solid", line.style);
    try testing.expectEqualStrings("#FFFF00", line.color);
}

test "a custom filter keeps its tint, and undo carries at least one step" {
    var s = try scriptCore.Script.parse("@source a.png:\n  @filter aqua\n  @undo\n");
    defer s.deinit();
    var buf: scriptCore.ResolveBuf = undefined;
    const f = decode(s, 1, .filter, 8, 8, &buf).?.filter;
    try testing.expectEqualStrings("custom", f.mode);
    try testing.expectEqualStrings("aqua", f.effective());
    try testing.expectEqual(@as(usize, 1), decode(s, 2, .undo, 8, 8, &buf).?.steps);
}

test "a save and a frame decode to the target and the index they name" {
    var s = try scriptCore.Script.parse("@source clip.mp4:\n  @frame 12\n  @save out.png\n");
    defer s.deinit();
    var buf: scriptCore.ResolveBuf = undefined;
    try testing.expectEqual(@as(u32, 12), decode(s, 1, .frame, 8, 8, &buf).?.frame);
    try testing.expectEqualStrings("out.png", decode(s, 2, .save, 8, 8, &buf).?.save);
}

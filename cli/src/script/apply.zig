//! One lowered op -> one edit on the canvas. Lengths are resolved here, against the image
//! as it stands right now, because a crop earlier in the block already changed its size.
const std = @import("std");

const core = @import("../core.zig");
const image = @import("../media/image.zig");
const layout_mod = @import("../media/layout.zig");
const pipeline = @import("../pipeline.zig");
const report = @import("../app/report.zig");
const scriptCore = @import("core.zig");

const decode = @import("decode.zig");

pub const Error = error{ScriptOpFailed};

/// The marks a block has placed, in the order the editors would stack them. They stay vectors over
/// the picture as the editors' lines do: never burned in, so a later filter cannot recolour them.
pub const Marks = struct {
    arena: std.heap.ArenaAllocator,
    items: std.ArrayList(core.LineDraw) = .empty,

    pub fn init(gpa: std.mem.Allocator) Marks {
        return .{ .arena = .init(gpa) };
    }

    pub fn deinit(self: *Marks) void {
        self.arena.deinit();
    }

    /// Copies `line` in, so the caller's resolve buffer or layout doc may go away.
    pub fn append(self: *Marks, line: core.LineDraw) !void {
        const a = self.arena.allocator();
        var copy = line;
        copy.points = try a.dupe(f64, line.points);
        copy.color = try a.dupeZ(u8, line.color);
        copy.style = try a.dupeZ(u8, line.style);
        copy.fill_color = try a.dupeZ(u8, line.fill_color);
        copy.point_color = try a.dupeZ(u8, line.point_color);
        try self.items.append(a, copy);
    }

    pub fn clear(self: *Marks) void {
        self.items = .empty;
        _ = self.arena.reset(.retain_capacity);
    }

    /// A crop moved the window from `old` to `new`: the marks follow core.cropChange, the rule every
    /// editor's lines follow — cleared on an album/portrait flip, else scaled by the width ratio.
    pub fn recrop(self: *Marks, old: core.Rect, new: core.Rect) void {
        const change = core.cropChange(old, new);
        if (change.orientation_changed) return self.clear();
        if (change.scale == 1) return;
        // `append` duped every mark's points into the arena, so they are this list's to change.
        for (self.items.items) |line| {
            for (@constCast(line.points)) |*v| v.* *= change.scale;
        }
    }

    /// `img` with every mark drawn over it, in order, as a new image the caller owns.
    pub fn render(self: *const Marks, gpa: std.mem.Allocator, img: image.Rgba8) !image.Rgba8 {
        const out: image.Rgba8 = .{ .width = img.width, .height = img.height, .pixels = try gpa.dupe(u8, img.pixels) };
        const w: i32 = @intCast(img.width);
        const h: i32 = @intCast(img.height);
        for (self.items.items) |line| core.rasterizeLine(out.pixels, w, h, line);
        return out;
    }
};

fn window(img: *const image.Rgba8) core.Rect {
    return .{ .x = 0, .y = 0, .w = @intCast(img.width), .h = @intCast(img.height) };
}

/// The run's parsed layout documents by source, so an @undo replay or the next input of a glob
/// reads a layout from here instead of fetching it again. Keys borrow the script's strings.
pub const Layouts = struct {
    docs: std.StringHashMapUnmanaged(layout_mod.Layout) = .empty,

    pub fn deinit(self: *Layouts, gpa: std.mem.Allocator) void {
        var it = self.docs.valueIterator();
        while (it.next()) |doc| doc.deinit();
        self.docs.deinit(gpa);
    }

    /// Valid until the next `get`: a new entry may move the table.
    pub fn get(self: *Layouts, gpa: std.mem.Allocator, io: std.Io, src: []const u8) !*const layout_mod.Layout {
        const slot = try self.docs.getOrPut(gpa, src);
        if (slot.found_existing) return slot.value_ptr;
        slot.value_ptr.* = pipeline.loadLayoutDoc(gpa, io, src) catch |e| {
            _ = self.docs.remove(src);
            return e;
        };
        return slot.value_ptr;
    }
};

pub fn applyOp(
    gpa: std.mem.Allocator,
    io: std.Io,
    script: scriptCore.Script,
    index: u32,
    op: scriptCore.Op,
    img: *image.Rgba8,
    marks: *Marks,
    layouts: *Layouts,
) !void {
    var buf: scriptCore.ResolveBuf = undefined;
    const edit = decode.decode(script, index, op.kind, @floatFromInt(img.width), @floatFromInt(img.height), &buf) orelse {
        report.err("line {d}: this {s} resolves to nothing\n", .{ op.line, if (op.kind == .crop) "crop" else "shape" });
        return Error.ScriptOpFailed;
    };

    switch (edit) {
        .crop => |rect| {
            const before = window(img);
            try pipeline.cropToRect(gpa, img, rect);
            marks.recrop(before, window(img));
        },
        .filter => |f| pipeline.applyFilterMode(gpa, img, f.effective()),
        .shape => |line| try marks.append(line),
        .layout => |l| {
            const doc = layouts.get(gpa, io, l.src) catch {
                report.err("line {d}: could not load the layout '{s}'\n", .{ op.line, l.src });
                return Error.ScriptOpFailed;
            };
            // "replace" drops every mark, as the editors replace the line model; the doc's
            // own lines otherwise stack on top of the marks already placed.
            if (std.mem.eql(u8, l.mode, "replace")) marks.clear();
            for (doc.lines) |line| try marks.append(line);
        },
        else => {},
    }
}

test "a mark survives the buffer it was resolved out of, and renders onto a copy" {
    const gpa = std.testing.allocator;
    var marks: Marks = .init(gpa);
    defer marks.deinit();

    {
        var pts = [_]f64{ 0, 0, 3, 3 };
        try marks.append(.{
            .points = &pts,
            .color = "#ff0000",
            .thickness = 2,
            .point_size = 0,
            .style = "solid",
            .locked = false,
            .fill_color = "transparent",
        });
        pts = .{ 9, 9, 9, 9 }; // the caller's buffer moves on; the mark must not
    }
    try std.testing.expectEqual(@as(usize, 1), marks.items.items.len);
    try std.testing.expectEqual(@as(f64, 3), marks.items.items[0].points[2]);

    const img: image.Rgba8 = .{ .width = 4, .height = 4, .pixels = try gpa.alloc(u8, 4 * 4 * 4) };
    defer gpa.free(img.pixels);
    @memset(img.pixels, 0);
    var drawn = try marks.render(gpa, img);
    defer drawn.deinit(gpa);
    try std.testing.expectEqual(@as(usize, 1), marks.items.items.len); // still vectors
    try std.testing.expect(drawn.pixels[0] != 0); // something was drawn
    try std.testing.expectEqual(@as(u8, 0), img.pixels[0]); // on the copy alone
}

test "a crop scales the marks by the width ratio, and clears them when it flips the orientation" {
    const gpa = std.testing.allocator;
    var marks: Marks = .init(gpa);
    defer marks.deinit();
    var pts = [_]f64{ 4, 5, 3, 2 };
    try marks.append(.{ .points = &pts, .color = "#ff0000", .thickness = 2, .point_size = 3, .style = "solid", .locked = false, .fill_color = "" });

    marks.recrop(.{ .x = 0, .y = 0, .w = 16, .h = 12 }, .{ .x = 4, .y = 3, .w = 8, .h = 6 });
    try std.testing.expectEqualSlices(f64, &.{ 2, 2.5, 1.5, 1 }, marks.items.items[0].points);
    try std.testing.expectEqual(@as(f64, 2), marks.items.items[0].thickness); // points only
    marks.recrop(.{ .x = 0, .y = 0, .w = 8, .h = 6 }, .{ .x = 0, .y = 0, .w = 4, .h = 6 });
    try std.testing.expectEqual(@as(usize, 0), marks.items.items.len);
}

test "a layout is read once per run: a replay finds it even after the file is gone" {
    const gpa = std.testing.allocator;
    var threaded = std.Io.Threaded.init(gpa, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const doc = "stencil_apply_cached_layout.json";
    try std.Io.Dir.cwd().writeFile(io, .{ .sub_path = doc, .data = "{\"lines\":[]}" });
    var layouts: Layouts = .{};
    defer layouts.deinit(gpa);
    _ = try layouts.get(gpa, io, doc);
    try std.Io.Dir.cwd().deleteFile(io, doc);
    _ = try layouts.get(gpa, io, doc);
    try std.testing.expectEqual(@as(u32, 1), layouts.docs.count());
}

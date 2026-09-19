//! One lowered op -> one edit on the canvas. Lengths are resolved here, against the image
//! as it stands right now, because a crop earlier in the block already changed its size.
const std = @import("std");

const core = @import("../core.zig");
const image = @import("../image.zig");
const pipeline = @import("../pipeline.zig");
const report = @import("../report.zig");
const scriptCore = @import("../scriptCore.zig");

const decode = @import("decode.zig");

pub const Error = error{ScriptOpFailed};

/// The marks a block has placed but not yet burned into the pixels, in the order the editors would
/// stack them. One arena per canvas: a mark's points and colours live until the next flush, no longer.
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

    /// Rasterizes every mark, in order, then clears. Called before each `@save` so the
    /// written file carries them, and before a `@crop` moves the frame they were placed in.
    pub fn burn(self: *Marks, img: *image.Rgba8) void {
        const w: i32 = @intCast(img.width);
        const h: i32 = @intCast(img.height);
        for (self.items.items) |line| core.rasterizeLine(img.pixels, w, h, line);
        self.clear();
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
) !void {
    var buf: scriptCore.ResolveBuf = undefined;
    const edit = decode.decode(script, index, op.kind, @floatFromInt(img.width), @floatFromInt(img.height), &buf) orelse {
        report.err("line {d}: this {s} resolves to nothing\n", .{ op.line, if (op.kind == .crop) "crop" else "shape" });
        return Error.ScriptOpFailed;
    };

    switch (edit) {
        .crop => |rect| {
            // Marks are in the pre-crop frame, so burn them before the frame moves.
            marks.burn(img);
            try pipeline.cropToRect(gpa, img, rect);
        },
        .filter => |f| pipeline.applyFilterMode(gpa, img, f.effective()),
        .shape => |line| try marks.append(line),
        .layout => |l| {
            var doc = pipeline.loadLayoutDoc(gpa, io, l.src) catch {
                report.err("line {d}: could not load the layout '{s}'\n", .{ op.line, l.src });
                return Error.ScriptOpFailed;
            };
            defer doc.deinit();
            // "replace" drops the marks not yet burned, as the editors replace the line
            // model; the doc's own lines then queue behind whatever survived.
            if (std.mem.eql(u8, l.mode, "replace")) marks.clear();
            for (doc.lines) |line| try marks.append(line);
        },
        else => {},
    }
}

test "a mark survives the buffer it was resolved out of, and burns onto the pixels" {
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

    var img: image.Rgba8 = .{ .width = 4, .height = 4, .pixels = try gpa.alloc(u8, 4 * 4 * 4) };
    defer gpa.free(img.pixels);
    @memset(img.pixels, 0);
    marks.burn(&img);
    try std.testing.expectEqual(@as(usize, 0), marks.items.items.len);
    try std.testing.expect(img.pixels[0] != 0); // something was drawn
}

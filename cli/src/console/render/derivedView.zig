//! The console's derived view, cached. A view is rotate → crop → filter over the untouched original,
//! and only then the drawn lines; every edit, undo and redo rebuilds it. Drawing a line, undoing
//! one or rendering an LLM variant leaves the triple alone, so `Base` keeps the last one it built
//! and hands out copies — which is what stops a `/line` re-running a contour convolution. Costs one
//! image of memory on top of the original and the current view.
const std = @import("std");
const image = @import("../../media/image.zig");
const core = @import("../../core.zig");
const pipeline = @import("../../pipeline.zig");

/// The snapshot fields the base is derived from — everything but the lines.
pub const Recipe = struct {
    rotation: i32 = 0,
    crop: ?core.Rect = null,
    mode: []const u8 = &.{},
    color: []const u8 = &.{},
};

pub const Base = struct {
    img: ?image.Rgba8 = null,
    // `mode`/`color` are owned copies: the snapshot they came from can be dropped.
    made: Recipe = .{},

    pub fn deinit(self: *Base, gpa: std.mem.Allocator) void {
        if (self.img) |*i| i.deinit(gpa);
        gpa.free(self.made.mode);
        gpa.free(self.made.color);
        self.* = .{};
    }

    fn holds(self: Base, want: Recipe) bool {
        if (self.img == null or self.made.rotation != want.rotation) return false;
        if (!rectEql(self.made.crop, want.crop)) return false;
        return std.mem.eql(u8, self.made.mode, want.mode) and std.mem.eql(u8, self.made.color, want.color);
    }

    /// A fresh copy of rotate → crop → filter applied to `orig` for `want`. Caller owns it.
    pub fn view(self: *Base, gpa: std.mem.Allocator, orig: image.Rgba8, want: Recipe) !image.Rgba8 {
        if (!self.holds(want)) try self.rebuild(gpa, orig, want);
        const cached = self.img.?;
        return .{ .width = cached.width, .height = cached.height, .pixels = try gpa.dupe(u8, cached.pixels) };
    }

    fn rebuild(self: *Base, gpa: std.mem.Allocator, orig: image.Rgba8, want: Recipe) !void {
        var img = image.Rgba8{ .width = orig.width, .height = orig.height, .pixels = try gpa.dupe(u8, orig.pixels) };
        errdefer img.deinit(gpa);
        if (@mod(want.rotation, 4) != 0) try pipeline.applyRotateBy(gpa, &img, want.rotation);
        if (want.crop) |cr| try pipeline.cropToRect(gpa, &img, cr);
        if (want.mode.len != 0 and !std.ascii.eqlIgnoreCase(want.mode, "none")) {
            const arg = if (std.ascii.eqlIgnoreCase(want.mode, "custom")) want.color else want.mode;
            pipeline.applyFilterMode(gpa, &img, arg);
        }
        const mode = try gpa.dupe(u8, want.mode);
        errdefer gpa.free(mode);
        const color = try gpa.dupe(u8, want.color);
        self.deinit(gpa);
        self.* = .{ .img = img, .made = .{ .rotation = want.rotation, .crop = want.crop, .mode = mode, .color = color } };
    }
};

fn rectEql(a: ?core.Rect, b: ?core.Rect) bool {
    const x = a orelse return b == null;
    const y = b orelse return false;
    return x.x == y.x and x.y == y.y and x.w == y.w and x.h == y.h;
}

const testing = std.testing;

fn ramp(a: std.mem.Allocator, w: usize, h: usize) !image.Rgba8 {
    const px = try a.alloc(u8, w * h * 4);
    for (px, 0..) |*p, i| p.* = @truncate(i);
    return .{ .width = w, .height = h, .pixels = px };
}

test "the base is reused until the rotate/crop/filter triple changes" {
    const a = testing.allocator;
    var orig = try ramp(a, 8, 6);
    defer orig.deinit(a);
    var base = Base{};
    defer base.deinit(a);

    var v1 = try base.view(a, orig, .{ .crop = .{ .x = 1, .y = 1, .w = 4, .h = 3 } });
    defer v1.deinit(a);
    const built = base.img.?.pixels.ptr;
    try testing.expectEqual(@as(usize, 4), v1.width);

    // Same triple (a line-only change): the cached base survives, the copy is independent.
    var v2 = try base.view(a, orig, .{ .crop = .{ .x = 1, .y = 1, .w = 4, .h = 3 } });
    defer v2.deinit(a);
    try testing.expectEqual(built, base.img.?.pixels.ptr);
    try testing.expect(v2.pixels.ptr != v1.pixels.ptr);
    try testing.expectEqualSlices(u8, v1.pixels, v2.pixels);

    // A different crop rebuilds it.
    var v3 = try base.view(a, orig, .{ .crop = .{ .x = 0, .y = 0, .w = 2, .h = 2 } });
    defer v3.deinit(a);
    try testing.expectEqual(@as(usize, 2), v3.width);
    try testing.expect(base.img.?.pixels.ptr != built);
}

test "a filter change rebuilds, and the cached mode outlives the caller's string" {
    const a = testing.allocator;
    var orig = try ramp(a, 8, 6);
    defer orig.deinit(a);
    var base = Base{};
    defer base.deinit(a);

    const mode = try a.dupe(u8, "bw");
    var v1 = try base.view(a, orig, .{ .mode = mode });
    defer v1.deinit(a);
    a.free(mode); // the snapshot is gone; the cache keeps its own copy
    var v2 = try base.view(a, orig, .{ .mode = "bw" });
    defer v2.deinit(a);
    try testing.expectEqualSlices(u8, v1.pixels, v2.pixels);

    var v3 = try base.view(a, orig, .{ .mode = "sepia" });
    defer v3.deinit(a);
    try testing.expect(!std.mem.eql(u8, v1.pixels, v3.pixels));
    // An unfiltered view differs again, and rotation is part of the recipe.
    var v4 = try base.view(a, orig, .{ .rotation = 1 });
    defer v4.deinit(a);
    try testing.expectEqual(@as(usize, 6), v4.width);
}

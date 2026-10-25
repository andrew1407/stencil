//! The console's single-item scrape under a size filter: each candidate it measures and drops is
//! freed before the next is fetched, so its memory peaks at one body, never at the whole page.
const std = @import("std");
const scrape = @import("../../src/scrape.zig");
const image = @import("../../src/media/image.zig");
const report = @import("../../src/app/report.zig");
const Served = @import("../script/served.zig").Served;
const File = @import("../script/served.zig").File;
const mkPng = @import("run_mock.zig").mkPng;
const testing = std.testing;
const Allocator = std.mem.Allocator;

// The most bytes held at once through it, every allocation counted until it is freed.
const Live = struct {
    child: Allocator,
    now: usize = 0,
    most: usize = 0,

    fn allocator(self: *Live) Allocator {
        return .{ .ptr = self, .vtable = &.{ .alloc = alloc, .resize = resize, .remap = remap, .free = free } };
    }
    fn grow(self: *Live, old: usize, new: usize) void {
        self.now = self.now - old + new;
        self.most = @max(self.most, self.now);
    }
    fn alloc(ctx: *anyopaque, len: usize, al: std.mem.Alignment, ra: usize) ?[*]u8 {
        const self: *Live = @ptrCast(@alignCast(ctx));
        const p = self.child.rawAlloc(len, al, ra) orelse return null;
        self.grow(0, len);
        return p;
    }
    fn resize(ctx: *anyopaque, mem: []u8, al: std.mem.Alignment, len: usize, ra: usize) bool {
        const self: *Live = @ptrCast(@alignCast(ctx));
        if (!self.child.rawResize(mem, al, len, ra)) return false;
        self.grow(mem.len, len);
        return true;
    }
    fn remap(ctx: *anyopaque, mem: []u8, al: std.mem.Alignment, len: usize, ra: usize) ?[*]u8 {
        const self: *Live = @ptrCast(@alignCast(ctx));
        const p = self.child.rawRemap(mem, al, len, ra) orelse return null;
        self.grow(mem.len, len);
        return p;
    }
    fn free(ctx: *anyopaque, mem: []u8, al: std.mem.Alignment, ra: usize) void {
        const self: *Live = @ptrCast(@alignCast(ctx));
        self.child.rawFree(mem, al, ra);
        self.grow(mem.len, 0);
    }
};

fn mute(_: *anyopaque, _: report.Severity, _: []const u8) void {}

test "scrapeOne: candidates that miss the size filter are freed one by one" {
    const a = testing.allocator;
    const big = 512 << 10; // each dropped candidate: a 200x200 PNG header padded to 512 KiB
    var bodies: [6][]u8 = undefined;
    for (&bodies) |*b| {
        b.* = try a.alloc(u8, big);
        @memset(b.*, 0);
        const head = mkPng(a, 200, 200);
        defer a.free(head);
        @memcpy(b.*[0..head.len], head);
    }
    defer for (bodies) |b| a.free(b);
    var px = [_]u8{ 1, 2, 3, 255 };
    const tiny = try image.encode(a, .{ .width = 1, .height = 1, .pixels = &px }, .png);
    defer a.free(tiny);
    const page = "<img src=\"/c0.png\"><img src=\"/c1.png\"><img src=\"/c2.png\"><img src=\"/c3.png\">" ++
        "<img src=\"/c4.png\"><img src=\"/c5.png\"><img src=\"/pick.png\">";
    const files = [_]File{
        .{ .path = "page.html", .body = page },   .{ .path = "c0.png", .body = bodies[0] },
        .{ .path = "c1.png", .body = bodies[1] }, .{ .path = "c2.png", .body = bodies[2] },
        .{ .path = "c3.png", .body = bodies[3] }, .{ .path = "c4.png", .body = bodies[4] },
        .{ .path = "c5.png", .body = bodies[5] }, .{ .path = "pick.png", .body = tiny },
    };
    const srv = try Served.start(&files);
    defer srv.stop();
    const url = try srv.url(a, "page.html");
    defer a.free(url);

    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var unused: u8 = 0;
    report.install(.{ .ctx = @ptrCast(&unused), .emitFn = mute });
    defer report.uninstall();
    var live = Live{ .child = a };
    var got = try scrape.scrapeOne(live.allocator(), threaded.io(), .{ .url = url, .max_width = 100 });
    defer {
        got.img.deinit(live.allocator());
        live.allocator().free(got.url);
    }
    try testing.expect(std.mem.endsWith(u8, got.url, "/pick.png"));
    try testing.expectEqual(@as(usize, 1), got.img.width);
    try testing.expect(live.most < 3 * big);
}

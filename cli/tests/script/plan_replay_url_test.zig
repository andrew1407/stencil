//! `--script-plan` over URLs, served on loopback: the planner sizes a `@source` URL whose block
//! draws lines through the guarded fetch `--script` opens it with, and carries a `@layout` URL's
//! lines like a local document's — so the replayed plans leave the one-shot's pixels. Under
//! the bot's surface the same loopback URL is refused, as the bot's own guard refuses it.
const std = @import("std");
const testing = std.testing;

const plan = @import("../../src/script/plan.zig");
const scriptCore = @import("../../src/script/core.zig");
const replay = @import("replay.zig");
const Served = @import("served.zig").Served;

const doc_json = "{\"lines\":[{\"points\":[{\"x\":0,\"y\":0},{\"x\":15,\"y\":11}],\"color\":\"#0000ff\",\"thickness\":2,\"pointSize\":0}]}";
const green = "@use line #00ff00 2px; @line (2,3) (14,9)";

fn serveFixtures(io: std.Io) !*Served {
    const png = try std.Io.Dir.cwd().readFileAlloc(io, "../common/samples/sample.png", testing.allocator, .limited(1 << 20));
    errdefer testing.allocator.free(png);
    const files = try testing.allocator.alloc(@import("served.zig").File, 2);
    files[0] = .{ .path = "sample.png", .body = png };
    files[1] = .{ .path = "doc.json", .body = doc_json };
    return Served.start(files);
}

fn release(server: *Served) void {
    const files = server.files;
    server.stop();
    testing.allocator.free(files[0].body);
    testing.allocator.free(files);
}

test "line, crop, line over a URL source, and a URL @layout added or replacing, replay as the one-shot" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const server = try serveFixtures(io);
    defer release(server);
    const a = testing.allocator;
    const src = try server.url(a, "sample.png");
    defer a.free(src);
    const head = try std.fmt.allocPrint(a, "@source {s}:\n", .{src});
    defer a.free(head);
    const doc = try server.url(a, "doc.json");
    defer a.free(doc);

    try replay.expectSame(io, head, green ++ "; @crop 25%; @line (1,1) (6,4)");
    const added = try std.fmt.allocPrint(a, green ++ "; @layout {s}; @line (1,1) (6,4)", .{doc});
    defer a.free(added);
    try replay.expectSame(io, head, added);
    const replaced = try std.fmt.allocPrint(a, green ++ "; @layout {s} replace; @crop 25%", .{doc});
    defer a.free(replaced);
    try replay.expectSame(io, head, replaced);
}

test "under the bot's surface a loopback URL is refused, as the bot's own guard refuses it" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const server = try serveFixtures(io);
    defer release(server);
    const a = testing.allocator;
    const src = try server.url(a, "sample.png");
    defer a.free(src);
    const text = try std.fmt.allocPrint(a, "@source {s}:\n  @line (0,0) (1,1)\n", .{src});
    defer a.free(text);
    var s = try scriptCore.Script.parse(text);
    defer s.deinit();
    var out: std.Io.Writer.Allocating = .init(a);
    defer out.deinit();
    try testing.expect(!try plan.envelope(a, io, &out.writer, s, .{ .plan_surface = "bot" }, "b.stc"));
    try testing.expect(std.mem.indexOf(u8, out.written(), "E_PLAN_SOURCE_UNREADABLE") != null);
}

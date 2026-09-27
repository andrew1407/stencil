// Integration: console /crop recalcs the drawn lines as the browser's crop does (applyCrop with
// recalc): a resize scales every point by the width ratio, an album/portrait flip clears them,
// and /undo brings them back. Driven through console.handle, the path the interactive loop uses.
const std = @import("std");
const console = @import("../../src/console.zig");
const testing = std.testing;

const layout_in = "stencil_console_crop_lines.json";
const layout_json = "{\"lines\":[{\"points\":[{\"x\":2,\"y\":3},{\"x\":7,\"y\":1}],\"color\":\"#00ff00\",\"pointSize\":3,\"thickness\":2}]}";

fn has(session: *console.Session, needle: []const u8) bool {
    return std.mem.indexOf(u8, session.state().lines(), needle) != null;
}

fn greenPixels(session: *console.Session) usize {
    const img = session.current().*;
    var n: usize = 0;
    var i: usize = 0;
    while (i < img.pixels.len) : (i += 4) n += @intFromBool(img.pixels[i + 1] > 200 and img.pixels[i] < 60);
    return n;
}

const Fixture = struct {
    threaded: std.Io.Threaded,
    session: console.Session,

    fn init(self: *Fixture) !void {
        self.* = .{ .threaded = std.Io.Threaded.init(testing.allocator, .{}), .session = .{ .gpa = testing.allocator } };
        try std.Io.Dir.cwd().writeFile(self.io(), .{ .sub_path = layout_in, .data = layout_json });
        _ = try console.handle(&self.session, self.io(), "/blank 16 12 red");
        _ = try console.handle(&self.session, self.io(), "/apply " ++ layout_in);
    }

    fn io(self: *Fixture) std.Io {
        return self.threaded.io();
    }

    fn deinit(self: *Fixture) void {
        std.Io.Dir.cwd().deleteFile(self.io(), layout_in) catch {};
        self.session.deinit();
        self.threaded.deinit();
    }
};

test "console: /crop to a smaller window of the same orientation scales the lines by the width ratio" {
    var f: Fixture = undefined;
    try f.init();
    defer f.deinit();
    const before = try testing.allocator.dupe(u8, f.session.state().lines());
    defer testing.allocator.free(before);

    // 16x12 → 8x6, both album: every point halves, wherever the window sits.
    _ = try console.handle(&f.session, f.io(), "/crop x1=25% x2=75% y1=25% y2=75%");
    try testing.expectEqual(@as(usize, 8), f.session.current().width);
    try testing.expect(has(&f.session, "{\"x\":1,\"y\":1.5},{\"x\":3.5,\"y\":0.5}"));
    try testing.expect(greenPixels(&f.session) > 0);

    _ = try console.handle(&f.session, f.io(), "/undo");
    try testing.expectEqualStrings(before, f.session.state().lines());
}

test "console: /crop that flips album to portrait clears the lines, and /undo restores them" {
    var f: Fixture = undefined;
    try f.init();
    defer f.deinit();
    const before = try testing.allocator.dupe(u8, f.session.state().lines());
    defer testing.allocator.free(before);

    _ = try console.handle(&f.session, f.io(), "/crop x1=0% x2=50% y1=0% y2=100%");
    try testing.expectEqual(@as(usize, 8), f.session.current().width);
    try testing.expectEqualStrings("[]", f.session.state().lines());
    try testing.expectEqual(@as(usize, 0), greenPixels(&f.session));

    _ = try console.handle(&f.session, f.io(), "/undo");
    try testing.expectEqualStrings(before, f.session.state().lines());
    try testing.expect(greenPixels(&f.session) > 0);
}

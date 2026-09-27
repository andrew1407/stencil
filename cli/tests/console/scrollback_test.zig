//! The full-screen scrollback's cap: past `max_lines` the oldest lines go in one batch off the
//! front of the ring (`screen.Lines`), so the rest never move and its storage is reused, and a
//! scrolled-up view stays on the text it showed.
const std = @import("std");
const screen = @import("../../src/console/screen.zig");
const testing = std.testing;

/// Print "line <from>" … "line <to - 1>" as one burst of output.
fn appendLines(s: *screen.Screen, from: usize, to: usize) !void {
    var out: std.ArrayList(u8) = .empty;
    defer out.deinit(testing.allocator);
    for (from..to) |i| try out.print(testing.allocator, "line {d}\n", .{i});
    s.append(out.items);
}

test "scrollback: past the cap the oldest lines leave in one batch, off the ring's front" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var s = screen.Screen{ .gpa = a, .io = threaded.io(), .fd = -1, .rows = 12, .cols = 40 };
    defer s.freeAllForTest();
    s.setRevealSpeed(1.0);
    const full = screen.max_lines + 512; // the batch waits until this many are held
    try appendLines(&s, 0, full);
    try testing.expectEqual(full, s.lines.len);
    try testing.expectEqual(@as(usize, 0), s.dropped);

    try appendLines(&s, full, full + 1);
    try testing.expectEqual(@as(usize, screen.max_lines), s.lines.len);
    try testing.expectEqual(@as(usize, 513), s.dropped);
    try testing.expectEqualStrings("line 513", s.lines.at(0));
    try testing.expectEqualStrings("line 5512", s.lines.back().?);

    // Scrolled up, the view keeps showing the same text across the next trim, and the ring
    // takes it all in the storage it already had.
    const storage = s.lines.buffer;
    s.scroll_off = 100;
    const shown = s.dropped + s.window().first;
    try appendLines(&s, full + 1, full + 514);
    try testing.expectEqual(@as(usize, screen.max_lines), s.lines.len);
    try testing.expectEqual(@as(usize, 1026), s.dropped);
    try testing.expectEqual(shown, s.dropped + s.window().first);
    try testing.expectEqual(@as(usize, 613), s.scroll_off);
    try testing.expectEqualStrings("line 1026", s.lines.at(0));
    try testing.expectEqual(storage.ptr, s.lines.buffer.ptr);
    try testing.expectEqual(storage.len, s.lines.buffer.len);
}

//! The cost of plain output on the full screen, counted on the fake terminal: one line
//! appended below a full 40-row body is one write() carrying one row, a partial line paints
//! nothing at all, scrolled up or not, skin or none, and a sent line's echo lands in one frame.
const std = @import("std");
const logo = @import("../../src/app/logo.zig");
const screen = @import("../../src/console/screen.zig");
const ansi = @import("../../src/console/render/ansi.zig");
const loop = @import("../../src/console/loop.zig");
const fx = @import("fx_harness.zig");
const skin = @import("../../src/app/skin.zig");
const testing = std.testing;

fn fullBody(rig: *fx.Rig) !void {
    try rig.start(testing.allocator, 54, 80);
    rig.scr.setRevealSpeed(screen.speed_max);
    try testing.expectEqual(@as(u16, 40), rig.scr.bodyRows());
    for (0..60) |i| logo.print("line {d} of plain output\n", .{i});
    rig.reset();
}

test "frame writes: one plain line below a full 40-row body is one write, one row" {
    var rig: fx.Rig = undefined;
    try fullBody(&rig);
    defer rig.deinit();
    logo.print("one more line\n", .{});
    try testing.expectEqual(@as(usize, 1), rig.writes);
    const bytes = rig.frame.items;
    try testing.expect(std.mem.startsWith(u8, bytes, "\x1b[?2026h\x1b[?25l"));
    try testing.expect(std.mem.endsWith(u8, bytes, "\x1b[?25h\x1b[?2026l"));
    // The body slides up one row inside its scroll region; only the new bottom row is drawn.
    try testing.expect(std.mem.indexOf(u8, bytes, "\x1b[12;51r\x1b[1S\x1b[r") != null);
    try testing.expectEqual(@as(usize, 1), std.mem.count(u8, bytes, "one more line"));
    try testing.expect(std.mem.indexOf(u8, bytes, "line 59") == null);
    try testing.expect(bytes.len < 160);
}

test "frame writes: a partial line paints nothing until it is complete" {
    var rig: fx.Rig = undefined;
    try fullBody(&rig);
    defer rig.deinit();
    logo.print("half a ", .{});
    try testing.expectEqual(@as(usize, 0), rig.writes);
    logo.print("line\n", .{});
    try testing.expectEqual(@as(usize, 1), rig.writes);
    try testing.expectEqualStrings("half a line", rig.scr.lines.back().?);
}

test "frame writes: under a secret skin a partial line paints nothing either" {
    var rig: fx.Rig = undefined;
    try fullBody(&rig);
    defer rig.deinit();
    skin.set(.meow);
    logo.print("half a ", .{});
    try testing.expectEqual(@as(usize, 0), rig.writes);
    logo.print("line\n", .{});
    try testing.expect(rig.writes != 0); // a skin repaints the whole body for the complete line
    try testing.expectEqualStrings("half a line", rig.scr.lines.back().?);
}

test "frame writes: a partial line while scrolled up paints nothing, not even the rules" {
    var rig: fx.Rig = undefined;
    try fullBody(&rig);
    defer rig.deinit();
    rig.scr.scroll(true, false);
    rig.reset();
    logo.print("half a ", .{});
    try testing.expectEqual(@as(usize, 0), rig.writes);
    logo.print("line\n", .{});
    try testing.expectEqual(@as(usize, 1), rig.writes); // the complete line moves the status bar
    try testing.expect(rig.scr.scroll_off != 0); // …and the view stays where the user left it
}

test "frame writes: a line sent while scrolled up returns to the bottom and echoes in one write" {
    var rig: fx.Rig = undefined;
    try fullBody(&rig);
    defer rig.deinit();
    rig.scr.scroll(true, false);
    rig.reset();
    loop.echoCommand(&rig.session, &rig.scr, "/help");
    try testing.expectEqual(@as(usize, 1), rig.writes);
    try testing.expectEqual(@as(usize, 0), rig.scr.scroll_off);
    const bytes = rig.frame.items;
    try testing.expectEqual(@as(usize, 1), std.mem.count(u8, bytes, "\x1b[?2026h"));
    try testing.expect(std.mem.indexOf(u8, bytes, "line 59 of plain output") != null); // the body, back at the bottom
    try testing.expect(std.mem.indexOf(u8, bytes, "/help") != null); // and the echo, in the same frame
    try testing.expect(std.mem.indexOf(u8, bytes, "\x1b[12;51r\x1b[3S\x1b[r") != null); // the body slides back…
    try testing.expectEqual(@as(usize, 0), std.mem.count(u8, bytes, "line 30 of")); // …not redrawn
}

test "frame writes: output that clears a highlight while scrolled up redraws only the lit row" {
    var rig: fx.Rig = undefined;
    try fullBody(&rig);
    defer rig.deinit();
    rig.scr.scroll(true, false);
    const row = rig.scr.bodyTop() + 2;
    rig.scr.selectForTest(row, 1, row, 12);
    rig.scr.repaintSelection();
    rig.reset();
    logo.print("more output\n", .{});
    try testing.expectEqual(@as(usize, 1), rig.writes);
    const bytes = rig.frame.items;
    try testing.expectEqual(@as(usize, 1), std.mem.count(u8, bytes, rig.scr.lineAtRow(row).?));
    try testing.expectEqual(@as(usize, 0), std.mem.count(u8, bytes, rig.scr.lineAtRow(row + 5).?));
}

test "frame writes: a line into a body with room left draws that row and nothing else" {
    var rig: fx.Rig = undefined;
    try rig.start(testing.allocator, 54, 80);
    defer rig.deinit();
    rig.scr.setRevealSpeed(screen.speed_max);
    rig.reset();
    logo.print("fifth line\n", .{});
    try testing.expectEqual(@as(usize, 1), rig.writes);
    const bytes = fx.stripWrappers(rig.frame.items);
    try testing.expectEqualStrings("\x1b[16;1Hfifth line\x1b[0m\x1b[K", bytes);
}

test "frame writes: the wheel slides the body instead of redrawing it" {
    var rig: fx.Rig = undefined;
    try fullBody(&rig);
    defer rig.deinit();
    rig.scr.scroll(true, false);
    try testing.expectEqual(@as(usize, 1), rig.writes);
    const bytes = rig.frame.items;
    try testing.expect(std.mem.indexOf(u8, bytes, "\x1b[12;51r\x1b[3T\x1b[r") != null);
    try testing.expectEqual(@as(usize, 1), std.mem.count(u8, bytes, "line 17 of")); // the one row that came into view…
    try testing.expectEqual(@as(usize, 0), std.mem.count(u8, bytes, "line 30 of")); // …of the three, the rest stay put
}

test "frame writes: three queued wheel notches slide the body once" {
    var rig: fx.Rig = undefined;
    try fullBody(&rig);
    defer rig.deinit();
    for (0..3) |_| rig.scr.scrollQuiet(true, false);
    try testing.expectEqual(@as(usize, 0), rig.writes); // nothing painted while notches queue
    rig.scr.settleScroll();
    try testing.expectEqual(@as(usize, 1), rig.writes);
    try testing.expect(std.mem.indexOf(u8, rig.frame.items, "\x1b[12;51r\x1b[9T\x1b[r") != null);
    rig.reset();
    rig.scr.settleScroll(); // settled: nothing more is owed
    try testing.expectEqual(@as(usize, 0), rig.writes);
}

test "frame writes: three queued drag reports paint the highlight once, where the last left it" {
    var rig: fx.Rig = undefined;
    try fullBody(&rig);
    defer rig.deinit();
    const row = rig.scr.bodyTop() + 2;
    rig.scr.selStart(1, row);
    rig.reset();
    for ([_]u16{ 4, 8, 12 }) |col| rig.scr.selDragQuiet(col, row);
    try testing.expectEqual(@as(usize, 0), rig.writes); // nothing painted while reports queue
    rig.scr.settleDrag();
    try testing.expectEqual(@as(usize, 1), rig.writes);
    var rb: [512]u8 = undefined;
    const lit = ansi.clipHighlight(rig.scr.lineAtRow(row).?, rig.scr.cols, 0, 12, &rb);
    try testing.expect(std.mem.indexOf(u8, rig.frame.items, lit) != null);
    rig.reset();
    rig.scr.settleDrag(); // settled: nothing more is owed
    try testing.expectEqual(@as(usize, 0), rig.writes);
}

test "frame writes: a spinner frame redraws its own row and nothing else" {
    var rig: fx.Rig = undefined;
    try rig.start(testing.allocator, 24, 80);
    defer rig.deinit();
    rig.scr.setRevealSpeed(screen.speed_max);
    logo.print("wait\n", .{});
    rig.reset();
    try testing.expect(rig.scr.replaceLine(4, "wait", "still waiting"));
    try testing.expectEqual(@as(usize, 1), rig.writes);
    try testing.expectEqualStrings("\x1b[16;1Hstill waiting\x1b[0m\x1b[K", fx.stripWrappers(rig.frame.items));
}

test "frame writes: output with control sequences keeps only its text and colour" {
    var rig: fx.Rig = undefined;
    try rig.start(testing.allocator, 24, 80);
    defer rig.deinit();
    rig.scr.setRevealSpeed(screen.speed_max);
    logo.print("\r\x1b[K\x1b]52;c;ZXZpbA==\x07\x1b[31mpeer\x1b[0m\x1b[?1049l edit\n", .{});
    try testing.expectEqualStrings("\x1b[31mpeer\x1b[0m edit", rig.scr.lines.back().?);
}

//! The colours the skins move through: the rainbow's hue per screen column (sliding a column a
//! frame under the bifrost), the selection wash each skin wears, and the fairy lights' bulbs,
//! one lit per turn. `skin.zig` re-exports them and owns the frame clock they read.
const std = @import("std");
const skin = @import("../skin.zig");
const theme = @import("../theme.zig");

const Skin = skin.Skin;

var bulb: usize = 0; // the fairy-light colour last lit
var bulb_due: u32 = 0; // the frame the next one lights on

/// A skin going on: the next bulb lights at once (the colour sequence carries on).
pub fn rewind() void {
    bulb_due = 0;
}

pub fn resetForTest() void {
    bulb = 0;
    bulb_due = 0;
}

// Degrees of hue per screen column: a full spectrum every ~16 cells.
const hue_step = 22;

/// The fg escape a run-painting skin gives the accent cell at screen column `col` (0-based).
pub fn cellSgr(s: Skin, col: usize, buf: []u8) []const u8 {
    const rgb = hueAt(s, col);
    return std.fmt.bufPrint(buf, "\x1b[38;2;{d};{d};{d}m", .{ rgb[0], rgb[1], rgb[2] }) catch "";
}

/// The selection wash's colour at screen column `col`: the skin's own, else the theme accent.
pub fn washRgb(s: Skin, col: usize, accent: [3]u8) [3]u8 {
    const t = skin.traitsOf(s);
    if (t.paints_runs) return hueAt(s, col);
    return t.wash orelse accent;
}

fn hueAt(s: Skin, col: usize) [3]u8 {
    const slid: i64 = @as(i64, @intCast(col)) - if (skin.traitsOf(s).slides) @as(i64, skin.frame()) else 0;
    const hue: f64 = @floatFromInt(@mod(slid * hue_step, 360));
    return theme.hsvToRgb(hue, 0.75, 1.0);
}

// Bulb colours bright enough to read on a dark terminal: red, gold, green, blue, pink, cyan.
const bulbs = [_][3]u8{ .{ 255, 70, 70 }, .{ 255, 200, 40 }, .{ 70, 225, 100 }, .{ 80, 150, 255 }, .{ 255, 100, 210 }, .{ 70, 225, 235 } };

// Each bulb stays lit this many frames — the `/theme` recolour wipe that lights it, then a rest.
const bulb_frames = 30;

/// The fairy lights' next colour, when its turn has come: the console lights it with the same
/// recolour wipe a `/theme` change runs, so the lights change at exactly that pace.
pub fn nextBulb() ?[3]u8 {
    const phase = skin.frame();
    if (phase < bulb_due) return null;
    bulb_due = phase + bulb_frames;
    bulb +%= 1;
    return bulbs[bulb % bulbs.len];
}

const testing = std.testing;

test "skin: the rainbow walks the hue wheel column by column" {
    var a: [24]u8 = undefined;
    var b: [24]u8 = undefined;
    try testing.expectEqualStrings("\x1b[38;2;255;64;64m", cellSgr(.rainbow, 0, &a));
    try testing.expect(!std.mem.eql(u8, cellSgr(.rainbow, 0, &a), cellSgr(.rainbow, 3, &b)));
}

test "skin: the bifrost slides its rainbow right" {
    var a: [24]u8 = undefined;
    var b: [24]u8 = undefined;
    skin.reseed(1);
    defer skin.reseed(0);
    const first = try testing.allocator.dupe(u8, cellSgr(.bifrost, 4, &a));
    defer testing.allocator.free(first);
    skin.tick();
    try testing.expectEqualStrings(first, cellSgr(.bifrost, 5, &b)); // one column on, one frame later
    try testing.expectEqual(hueAt(.rainbow, 4), washRgb(.rainbow, 4, .{ 0, 0, 0 }));
    try testing.expectEqual([3]u8{ 1, 2, 3 }, washRgb(.none, 4, .{ 1, 2, 3 }));
}

test "skin: a fairy light stays lit for its turn, then the next one comes" {
    skin.reseed(0);
    defer skin.reseed(0);
    const first = nextBulb().?;
    try testing.expect(nextBulb() == null);
    for (0..bulb_frames - 1) |_| skin.tick();
    try testing.expect(nextBulb() == null);
    skin.tick();
    try testing.expect(!std.meta.eql(first, nextBulb().?));
}

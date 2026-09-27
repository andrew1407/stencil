//! Frame pins for the console's own effects: entering the full screen, the logo press, the
//! recolour wipe (a typed /theme, a logo click, a double-click's custom colour), the reveal
//! sweep at the default and a custom speed, /eastereggs, and a secret word typed in gold.
const std = @import("std");
const hooks = @import("../../src/console/hooks.zig");
const logo = @import("../../src/app/logo.zig");
const line_edit = @import("../../src/line_edit/line_edit.zig");
const fx = @import("fx_harness.zig");
const testing = std.testing;

test "fx pins: entering the full screen" {
    var rig: fx.Rig = undefined;
    try rig.init(testing.allocator, 24, 80);
    defer rig.deinit();
    rig.mark("enter");
    try rig.scr.enter();
    try rig.expectPin("enter");
}

test "fx pins: the logo press" {
    var rig: fx.Rig = undefined;
    try rig.start(testing.allocator, 24, 80);
    defer rig.deinit();
    rig.mark("press");
    rig.scr.pressLogo();
    try rig.expectPin("press");
}

test "fx pins: /theme wipes the new accent across" {
    var rig: fx.Rig = undefined;
    try rig.start(testing.allocator, 24, 80);
    defer rig.deinit();
    try rig.typeLine("/theme blue");
    try rig.expectPin("wipe-theme");
}

test "fx pins: a logo click cycles the accent, a double-click picks a custom one" {
    var rig: fx.Rig = undefined;
    try rig.start(testing.allocator, 24, 80);
    defer rig.deinit();
    rig.mark("click");
    hooks.logoCycle(&rig.idle_ctx);
    rig.mark("click");
    hooks.logoCycle(&rig.idle_ctx);
    rig.mark("double-click");
    hooks.logoCustom(&rig.idle_ctx);
    try rig.expectPin("accent-cycle");
}

test "fx pins: the reveal sweep at the default speed — a lone line, a burst, a pause" {
    var rig: fx.Rig = undefined;
    try rig.start(testing.allocator, 24, 80);
    defer rig.deinit();
    rig.mark("lone line");
    logo.print("a line sweeping in from the left\n", .{});
    rig.mark("burst");
    logo.print("hard on its heels\n", .{});
    logo.print("three\nlines\nat once\n", .{});
    rig.pause(400);
    rig.mark("after a pause");
    logo.print("{s}accented{s} and a line long enough to wrap past the eighty columns of this terminal\n", .{ logo.accentSeq(), logo.resetSeq() });
    try rig.expectPin("reveal-default");
}

test "fx pins: /reveal-speed sets a custom sweep" {
    var rig: fx.Rig = undefined;
    try rig.start(testing.allocator, 24, 80);
    defer rig.deinit();
    try rig.typeLine("/reveal-speed 0.2");
    rig.pause(2000);
    rig.mark("line");
    logo.print("slower now\n", .{});
    try rig.typeLine("/reveal-speed 0.9");
    rig.pause(2000);
    rig.mark("line");
    logo.print("faster now\n", .{});
    try rig.expectPin("reveal-custom");
}

test "fx pins: /eastereggs lists the words in gold" {
    var rig: fx.Rig = undefined;
    try rig.start(testing.allocator, 24, 80);
    defer rig.deinit();
    try rig.typeLine("/eastereggs");
    try rig.expectPin("eastereggs");
}

test "fx pins: a secret word types in gold on the prompt row" {
    var rig: fx.Rig = undefined;
    try rig.start(testing.allocator, 24, 80);
    defer rig.deinit();
    var ed = line_edit.Editor{ .fd_in = -1, .fd_out = fx.fake_fd, .orig = std.mem.zeroes(std.posix.termios), .screen = &rig.scr };
    for ([_][]const u8{ "/me", "/meow", "/MrAnderson now", "/theme blue" }) |line| {
        rig.mark(line);
        ed.refresh("> ", line, line.len);
    }
    try rig.expectPin("gold-prompt");
}

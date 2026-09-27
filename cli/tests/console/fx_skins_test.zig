//! Frame pins for the eight secret skins: the gold echo of the typed word, the skin going on
//! (its logo art, colours and the terminal's own OSC 10/11), a second of its idle animation
//! on the 80 ms clock, and the word again taking it off (OSC 110/111 hands the colours back).
const std = @import("std");
const fx = @import("fx_harness.zig");
const testing = std.testing;

/// One skin's life: typed on, `beats` idle frames, typed off.
fn skinPin(comptime word: []const u8, beats: usize) !void {
    var rig: fx.Rig = undefined;
    try rig.start(testing.allocator, 24, 80);
    defer rig.deinit();
    try rig.typeLine("/" ++ word);
    rig.dumpLines("header on", rig.scr.header.items);
    rig.mark("idle");
    rig.idle(beats);
    try rig.typeLine("/" ++ word);
    rig.dumpLines("header off", rig.scr.header.items);
    try rig.expectPin("skin-" ++ word);
}

test "fx pins: /mranderson — the matrix, its rain and the terminal's colours" {
    try skinPin("mranderson", 3);
}

test "fx pins: /theverybluescreen — the blue screen and its title bar" {
    try skinPin("theverybluescreen", 3);
}

test "fx pins: /sunafterrain — the rainbow" {
    try skinPin("sunafterrain", 3);
}

test "fx pins: /theyareinthetrees — fruit for letters" {
    try skinPin("theyareinthetrees", 14);
}

test "fx pins: /meow — the cat in the logo" {
    try skinPin("meow", 14);
}

test "fx pins: /pieday — the P and its pies" {
    try skinPin("pieday", 14);
}

test "fx pins: /bifrost — the M and the sliding rainbow" {
    try skinPin("bifrost", 14);
}

test "fx pins: /fairylight — a bulb lights with the recolour wipe" {
    try skinPin("fairylight", 14);
}

test "fx pins: one skin straight onto another, then /theme takes it off" {
    var rig: fx.Rig = undefined;
    try rig.start(testing.allocator, 24, 80);
    defer rig.deinit();
    try rig.typeLine("/meow");
    try rig.typeLine("/mranderson");
    try rig.typeLine("/theme default");
    try rig.expectPin("skin-switch");
}

//! The pictures the `/theyareinthetrees`, `/meow` and `/pieday` skins put in place of accent
//! letters. `skin.zig` re-exports them.
const std = @import("std");
const skin = @import("../skin.zig");

// Every fruit of the Unicode "fruit" search but the hieroglyph, plus coconut and olive; each is
// two cells wide (the lime is lemon + ZWJ + green square, drawn as one).
pub const fruits = [_][]const u8{ "🍅", "🍇", "🍈", "🍉", "🍊", "🍋", "🍋\u{200d}🟩", "🍌", "🍍", "🍎", "🍏", "🍐", "🍑", "🍒", "🍓", "🥑", "🥝", "🥧", "🥭", "🫐", "🥥", "🫒" };
pub const pies = [_][]const u8{ "🥧", "🥮" };
pub const cats = [_][]const u8{ "🐱", "🦁", "🐯" };

// Each picture swaps for another every `swap_frames` frames, each on its own beat, so the
// row shimmers — every emoji sits a little differently in its cell, which reads as a small hop.
const swap_frames = 83; // ~6.6s a picture: at ~150 on screen, a gentle few swaps a second

/// The picture for the letter at `col` of the row hashed to `row_key`, never `prev` again:
/// the same on every repaint of a frame, a new one when that cell's beat comes round.
pub fn pictureAt(s: skin.Skin, row_key: u64, col: usize, prev: ?usize) usize {
    const cell = std.hash.Wyhash.hash(skin.seedValue() ^ row_key, std.mem.asBytes(&col));
    const beat: u64 = (skin.frame() + cell % swap_frames) / swap_frames;
    var h = std.hash.Wyhash.hash(cell, std.mem.asBytes(&beat));
    if (s == .pie) return if (h % 5 == 0) 1 else 0; // a pie, now and then a mooncake
    const n: usize = if (s == .meow) cats.len else fruits.len;
    var i: usize = @intCast(h % n);
    if (prev == i) {
        h >>= 8;
        i = (i + 1 + @as(usize, @intCast(h % (n - 1)))) % n;
    }
    return i;
}

pub fn picture(s: skin.Skin, i: usize) []const u8 {
    return switch (s) {
        .meow => cats[i],
        .pie => pies[i],
        else => fruits[i],
    };
}

const testing = std.testing;

test "skin: no picture repeats its neighbour, and a row repaints with the same ones" {
    for ([_]skin.Skin{ .fruit, .meow }) |s| {
        var prev: ?usize = null;
        for (0..400) |col| {
            const i = pictureAt(s, 42, col, prev);
            try testing.expect(prev != i);
            try testing.expectEqual(i, pictureAt(s, 42, col, prev));
            prev = i;
        }
    }
    for (fruits) |f| try testing.expect(!std.mem.eql(u8, f, "\u{131E8}"));
}

test "skin: a picture swaps on its own beat, and a frame repaints the same" {
    skin.reseed(7);
    defer skin.reseed(0);
    const first = pictureAt(.fruit, 42, 10, null);
    try testing.expectEqual(first, pictureAt(.fruit, 42, 10, null));
    var changed = false;
    for (0..swap_frames * 8) |_| {
        skin.tick();
        if (pictureAt(.fruit, 42, 10, null) != first) changed = true;
    }
    try testing.expect(changed);
}

test "skin: pie mode is mostly pie, with a mooncake flickering through" {
    skin.reseed(3);
    defer skin.reseed(0);
    var pie: usize = 0;
    var cake: usize = 0;
    for (0..60) |f| {
        _ = f;
        for (0..40) |col| {
            if (pictureAt(.pie, 9, col * 2, null) == 0) pie += 1 else cake += 1;
        }
        skin.tick();
    }
    try testing.expect(cake > 0 and pie > cake * 2);
}

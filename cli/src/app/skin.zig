//! The console's secret skins — reached only by typing their word as a command, slash and all,
//! never listed by `help` or Tab: one `Traits` row per skin (its word, what it says, how it
//! paints), which one is on and the animation clock. `console/render/ansi/restyle.zig` applies
//! the skin to every row the full-screen console paints.
const std = @import("std");
const msg = @import("messages.zig");
const pictures = @import("skin/pictures.zig");
const cat = @import("skin/cat.zig");
const hues = @import("skin/hues.zig");

pub const fruits = pictures.fruits;
pub const pictureAt = pictures.pictureAt;
pub const picture = pictures.picture;
pub const catPose = cat.catPose;
pub const cellSgr = hues.cellSgr;
pub const washRgb = hues.washRgb;
pub const nextBulb = hues.nextBulb;

pub const Skin = enum { none, matrix, bluescreen, rainbow, fruit, meow, pie, bifrost, fairylight };

pub const list_word = "eastereggs";
// Gold is the one colour no skin rewrites: secret words, their list and phrases, on every skin.
pub const gold = "\x1b[1;38;2;255;215;0m";
// What the theme accent becomes under the picture skins: the same gold, not bold.
pub const gold_accent = "\x1b[38;2;255;215;0m";
// OSC 10/11 set the terminal's default fg/bg, so cells no row paints match the skin too;
// OSC 110/111 hand the terminal's own colours back.
const osc_reset = "\x1b]110\x07\x1b]111\x07";

/// What a skin is. `.none` is the console's own look; a skin with no `word` cannot be typed on.
pub const Traits = struct {
    word: []const u8 = "",
    phrase: []const u8 = "", // said as it goes on; the blue screen has a whole screen instead
    phrase_off: []const u8 = "",
    wordmark: []const u8 = "S T E N C I L",
    wordmark_accented: bool = false, // set in the accent rather than plain
    animated: bool = false, // moves on its own, so the idle prompt keeps repainting it
    moves_logo: bool = false, // the animation moves the logo art, so each frame recaptures the header
    paints_runs: bool = false, // each accent cell in its column's hue, not one escape per span
    slides: bool = false, // the hues move one column right per frame
    paints_cells: bool = false, // fg + bg of every cell; `osc` makes the terminal's own colours follow
    replaces_letters: bool = false, // each accent letter becomes a picture
    base: []const u8 = "", // the plain cell style: a reset, then fg + bg, so an erase-to-EOL fills its bg
    title: []const u8 = "", // the inverted title bar
    osc: []const u8 = osc_reset,
    wash: ?[3]u8 = null, // the selection wash; null = the theme accent
};

const traits = std.EnumArray(Skin, Traits).init(.{
    .none = .{},
    .matrix = .{
        .word = "mranderson",
        .phrase = msg.mranderson_on,
        .paints_cells = true,
        .base = "\x1b[0;38;2;35;209;139;48;2;0;0;0m",
        .title = "\x1b[0;38;2;0;0;0;48;2;35;209;139m",
        .osc = "\x1b]10;#23d18b\x07\x1b]11;#000000\x07",
        .wash = .{ 35, 209, 139 },
    },
    .bluescreen = .{
        .word = "theverybluescreen",
        .wordmark = "T H E  B L U E  S C R E E N  O F  D E A T H",
        .paints_cells = true,
        .base = "\x1b[0;38;2;255;255;255;48;2;0;0;170m",
        .title = "\x1b[0;38;2;0;0;170;48;2;170;170;170m",
        .osc = "\x1b]10;#ffffff\x07\x1b]11;#0000aa\x07",
        .wash = .{ 170, 170, 170 },
    },
    .rainbow = .{ .word = "sunafterrain", .phrase = msg.sunafterrain_on, .wordmark_accented = true, .paints_runs = true },
    .fruit = .{ .word = "theyareinthetrees", .phrase = msg.fruit_on, .wordmark_accented = true, .animated = true, .replaces_letters = true, .wash = .{ 255, 140, 0 } },
    .meow = .{ .word = "meow", .phrase = msg.meow_on, .phrase_off = msg.meow_off, .wordmark_accented = true, .animated = true, .moves_logo = true, .replaces_letters = true, .wash = .{ 255, 170, 60 } },
    .pie = .{ .word = "pieday", .phrase = msg.pieday_on, .wordmark = "S T E N C I L  P I E", .animated = true, .replaces_letters = true, .wash = .{ 205, 140, 70 } },
    .bifrost = .{ .word = "bifrost", .phrase = msg.bifrost_on, .wordmark = "M I D G A R D", .wordmark_accented = true, .animated = true, .paints_runs = true, .slides = true },
    .fairylight = .{ .word = "fairylight", .phrase = msg.fairylight_on, .wordmark = "B A D  S T E N C I L", .wordmark_accented = true, .animated = true },
});

pub fn traitsOf(s: Skin) *const Traits {
    return traits.getPtrConst(s);
}

var current: Skin = .none;
var seed: u64 = 0;
var phase: u32 = 0; // animation frames since the skin went on
var clock_start_ms: i64 = 0; // when the animation's frame 0 was
pub const frame_ms = 80; // one animation frame; the idle prompt polls at this pace while a skin moves

pub fn get() Skin {
    return current;
}

pub fn set(s: Skin) void {
    current = s;
}

/// Whether the skin that is on moves on its own.
pub fn animating() bool {
    return traitsOf(current).animated;
}

/// Frames since the animation started.
pub fn frame() u32 {
    return phase;
}

/// The seed of the current draw of pictures.
pub fn seedValue() u64 {
    return seed;
}

/// A new draw of fruit, animals and cat face — each time a skin goes on.
pub fn reseed(s: u64) void {
    seed = s;
    phase = 0;
    hues.rewind();
}

/// Back to the console's own look with every counter at zero, the fairy lights included.
pub fn resetForTest() void {
    current = .none;
    reseed(0);
    clock_start_ms = 0;
    hues.resetForTest();
}

/// Start the animation clock: frame 0 is now.
pub fn startClock(now_ms: i64) void {
    clock_start_ms = now_ms;
    phase = 0;
}

/// Bring the animation to the frame the clock says; true when that moved it, so the caller
/// repaints. Clock-driven, so the pace holds while the console is busy printing.
pub fn syncClock(now_ms: i64) bool {
    const want: u32 = @intCast(@max(0, @divFloor(now_ms - clock_start_ms, frame_ms)));
    if (want == phase) return false;
    phase = want;
    return true;
}

/// Advance an animated skin one frame.
pub fn tick() void {
    phase +%= 1;
}

/// The skin a typed command word switches, or null when it names none.
pub fn skinOf(word: []const u8) ?Skin {
    for (std.enums.values(Skin)) |s| {
        const own = traitsOf(s).word;
        if (own.len != 0 and std.ascii.eqlIgnoreCase(own, word)) return s;
    }
    return null;
}

/// Whether `word` is one of the secret commands (the list word included) — these type in gold.
pub fn isSecret(word: []const u8) bool {
    return std.ascii.eqlIgnoreCase(word, list_word) or skinOf(word) != null;
}

/// Whether the skin re-dresses painted rows at all; the fairy lights recolour the accent itself.
pub fn restyles(s: Skin) bool {
    const t = traitsOf(s);
    return t.paints_cells or t.paints_runs or t.replaces_letters;
}

/// What leaving the full-screen console writes: the terminal's colours back when a skin took them.
pub fn leaveSeq() []const u8 {
    const leave = "\x1b[?7h\x1b[?1049l";
    return if (traitsOf(current).paints_cells) osc_reset ++ leave else leave;
}

const testing = std.testing;

test "skin: secret words map to skins, case-insensitively, and nothing else does" {
    try testing.expectEqual(Skin.matrix, skinOf("MRANDERSON").?);
    try testing.expect(skinOf("matrix") == null);
    try testing.expectEqual(Skin.fruit, skinOf("theyareinthetrees").?);
    try testing.expect(skinOf("makeitfruit") == null);
    try testing.expectEqual(Skin.meow, skinOf("meow").?);
    try testing.expectEqual(Skin.pie, skinOf("PieDay").?);
    try testing.expectEqual(Skin.bluescreen, skinOf("theverybluescreen").?);
    try testing.expectEqual(Skin.rainbow, skinOf("sunafterrain").?);
    try testing.expect(skinOf("eastereggs") == null);
    try testing.expect(skinOf("theme") == null);
    try testing.expect(skinOf("") == null); // `.none` has no word, so an empty word is not it
    try testing.expect(isSecret("EasterEggs"));
    try testing.expect(isSecret("meow"));
    try testing.expect(!isSecret("matrix"));
    try testing.expect(!isSecret("help"));
}

test "skin: every skin but none has a word, and only the picture skins swap letters" {
    for (std.enums.values(Skin)) |s| {
        const t = traitsOf(s);
        try testing.expectEqual(s != .none, t.word.len != 0);
        try testing.expectEqual(t.replaces_letters, t.wash != null and !t.paints_cells);
        try testing.expectEqual(t.paints_cells, t.base.len != 0);
        try testing.expectEqual(restyles(s), s != .none and s != .fairylight);
    }
}

test "skin: the animation clock moves a frame every 80ms, never backwards" {
    defer reseed(0);
    startClock(1000);
    try testing.expect(!syncClock(1079));
    try testing.expect(syncClock(1080));
    try testing.expect(!syncClock(1100));
    try testing.expect(syncClock(1000 + 80 * 7));
    try testing.expect(syncClock(500)); // a clock before the start holds frame 0
    try testing.expect(!syncClock(500));
}

test {
    _ = pictures;
    _ = cat;
    _ = hues;
}

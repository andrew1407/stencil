//! The two screen preferences a command can change: the reveal speed's 0.01…1 scale and
//! the accent the pinned logo cycles through.
const std = @import("std");
const sc = @import("../screen.zig");

const reveal_speed_min = sc.reveal_speed_min;
const reveal_speed_max = sc.reveal_speed_max;
const reveal_speed_default = sc.reveal_speed_default;
const theme = @import("../../theme.zig");

pub fn parseRevealSpeed(text: []const u8) ?f64 {
    const v = std.fmt.parseFloat(f64, std.mem.trim(u8, text, " \t")) catch return null;
    if (std.math.isNan(v) or v < reveal_speed_min or v > reveal_speed_max) return null;
    return v;
}

/// The scale's ends and default, for the `/reveal-speed` command's messages.
pub const speed_min = reveal_speed_min;
pub const speed_max = reveal_speed_max;
pub const speed_default = reveal_speed_default;

/// The next accent key when the logo is single-clicked: advance through the preset list
/// (wrapping), or reset to the default when a custom colour (`current` is a '#hex') is active
/// — exactly what the browser does. `current` is the active accent key.
pub fn nextAccentKey(cur: []const u8) []const u8 {
    if (cur.len != 0 and cur[0] == '#') return theme.default_key;
    var idx: usize = 0;
    const all = theme.accents();
    for (all, 0..) |a, i| {
        if (std.ascii.eqlIgnoreCase(a.key, cur)) {
            idx = i;
            break;
        }
    }
    return all[(idx + 1) % all.len].key;
}

// tests (pure helpers only; the terminal path never runs in CI)

const testing = std.testing;

test "nextAccentKey: advances presets, wraps, resets from custom" {
    try testing.expectEqualStrings("burgundy", nextAccentKey("violet")); // first -> second
    try testing.expectEqualStrings("violet", nextAccentKey("blue")); // last wraps to first
    try testing.expectEqualStrings("violet", nextAccentKey("#ff8800")); // custom -> default
    try testing.expectEqualStrings("burgundy", nextAccentKey("VIOLET")); // case-insensitive
}
test "parseRevealSpeed: the 0.01 … 1 scale, and what is not on it" {
    try testing.expectEqual(@as(f64, 0.5), parseRevealSpeed("0.5").?);
    try testing.expectEqual(@as(f64, 1.0), parseRevealSpeed("1").?);
    try testing.expectEqual(@as(f64, 0.01), parseRevealSpeed("0.01").?);
    try testing.expectEqual(@as(f64, 0.25), parseRevealSpeed(" .25 ").?); // padded, leading dot
    try testing.expect(parseRevealSpeed("0") == null); // never finishes — 0.01 is the floor
    try testing.expect(parseRevealSpeed("0.009") == null);
    try testing.expect(parseRevealSpeed("1.5") == null); // 1 is already instant
    try testing.expect(parseRevealSpeed("-1") == null);
    try testing.expect(parseRevealSpeed("fast") == null);
    try testing.expect(parseRevealSpeed("") == null);
    try testing.expect(parseRevealSpeed("nan") == null);
}

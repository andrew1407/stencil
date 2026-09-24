//! The two screen preferences a command can change: the reveal speed's 0.01…1 scale and
//! the accent the pinned logo cycles through.
const std = @import("std");
const theme = @import("../../app/theme.zig");

// `/reveal-speed <speed>`, 0.01 … 1: 1 = instant, smaller = slower (0 would never finish). The sweep's
// constants are the pace at the default 0.5; each scales by (1-speed)/speed, which is 1 there.
pub const speed_min = 0.01;
pub const speed_max = 1.0;
pub const speed_default = 0.5;

pub fn parseRevealSpeed(text: []const u8) ?f64 {
    const v = std.fmt.parseFloat(f64, std.mem.trim(u8, text, " \t")) catch return null;
    if (std.math.isNan(v) or v < speed_min or v > speed_max) return null;
    return v;
}

/// The next accent key when the logo is single-clicked: advance through the preset list (wrapping),
/// or reset to the default when a custom colour (`cur` is a '#hex') is active — as the browser does.
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

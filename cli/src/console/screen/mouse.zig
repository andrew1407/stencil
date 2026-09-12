//! SGR mouse reports: the parsed record and the button/wheel predicates the input loop
//! classifies a press, drag, release or wheel notch with.
const std = @import("std");

pub const Mouse = struct {
    btn: u16, // full SGR button code: low 2 bits = button, +64 = wheel, higher bits = modifiers
    col: u16, // 1-based
    row: u16, // 1-based
    press: bool, // true = press ('M'), false = release ('m')

    pub fn isWheelUp(m: Mouse) bool {
        return (m.btn & 64) != 0 and (m.btn & 1) == 0;
    }
    pub fn isWheelDown(m: Mouse) bool {
        return (m.btn & 64) != 0 and (m.btn & 1) == 1;
    }
    // A wheel notch has bit 64; drag-motion has bit 32. A plain left-button press is neither.
    pub fn isLeftPress(m: Mouse) bool {
        return m.press and (m.btn & 64) == 0 and (m.btn & 32) == 0 and (m.btn & 3) == 0;
    }
    // Motion while the left button is held (SGR sets bit 32 on drag reports) — a text drag.
    pub fn isLeftDrag(m: Mouse) bool {
        return m.press and (m.btn & 32) != 0 and (m.btn & 64) == 0 and (m.btn & 3) == 0;
    }
    pub fn isRelease(m: Mouse) bool {
        return !m.press;
    }
};

/// Parse the body of an SGR mouse report — the bytes after the `ESC [ <` intro, including the
/// terminating 'M' (press) or 'm' (release): `btn ; col ; row (M|m)`. Returns null on garbage.
pub fn parseMouse(seq: []const u8) ?Mouse {
    if (seq.len < 6) return null;
    const last = seq[seq.len - 1];
    if (last != 'M' and last != 'm') return null;
    var it = std.mem.splitScalar(u8, seq[0 .. seq.len - 1], ';');
    const btn = std.fmt.parseInt(u16, it.next() orelse return null, 10) catch return null;
    const col = std.fmt.parseInt(u16, it.next() orelse return null, 10) catch return null;
    const row = std.fmt.parseInt(u16, it.next() orelse return null, 10) catch return null;
    if (it.next() != null) return null;
    return .{ .btn = btn, .col = col, .row = row, .press = last == 'M' };
}

// accent cycle (mirrors browser/js/ui/toolbar.js cycleAccent)

const testing = std.testing;

test "parseMouse: SGR press/release, wheel classification" {
    const p = parseMouse("0;10;3M").?;
    try testing.expect(p.press and p.isLeftPress());
    try testing.expectEqual(@as(u16, 10), p.col);
    try testing.expectEqual(@as(u16, 3), p.row);
    try testing.expect(parseMouse("0;10;3m").?.press == false);
    try testing.expect(parseMouse("64;5;5M").?.isWheelUp());
    try testing.expect(parseMouse("65;5;5M").?.isWheelDown());
    try testing.expect(parseMouse("garbage") == null);
    try testing.expect(parseMouse("1;2X") == null);
}

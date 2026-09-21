//! The S icon's clock turn: a hand pivots on the icon's middle and sweeps the new accent
//! round it while the seam takes the rest of the row. Pure geometry over the cell grid.
const std = @import("std");
const logo = @import("../../../app/logo.zig");
const ansi = @import("../ansi.zig");
const screen_mod = @import("../../screen.zig");
const Screen = screen_mod.Screen;
const Span = ansi.Span;

// Columns the logo icon owns. The full banner's widest row ends at 20 and the wordmark starts
// at 24, so the press frame repaints exactly this much and leaves the wordmark alone.
pub const icon_cols = 21;

// The S icon recolours as a clock instead of taking the seam: a hand pivots on its middle, one full
// clockwise turn over the seam's travel. Rows count `cell_aspect` times as far as columns.
const wipe_degrees = 360.0;
const cell_aspect = 2.0;
// The hand turns a shade faster than the seam (1.25×), so the icon is home at 12 o'clock
// a little before the text finishes.
const icon_speed = 1.25;

/// The angle of a cell from the pivot, degrees clockwise from 12 o'clock (up = 0, right = 90). Rows
/// count `cell_aspect` times as far as columns; the pivot answers 0, so the first frame sweeps it.
pub fn cellAngle(row: f64, col: f64, cy: f64, cx: f64) f64 {
    const dx = col - cx;
    const dy = (cy - row) * cell_aspect; // up is positive, like a clock face
    if (dx == 0 and dy == 0) return 0;
    const deg = std.math.radiansToDegrees(std.math.atan2(dx, dy));
    return if (deg < 0) deg + 360 else deg;
}

/// The stretches of one row a hand `deg` into its clockwise turn has passed, written into `out`.
/// Returns 0, 1 or 2 — two when the swept sector has wrapped back over 12 o'clock.
pub fn sweptSpansAt(row: u16, cols: u16, cy: f64, cx: f64, deg: f64, out: *[2]Span) usize {
    if (deg <= 0 or cols == 0) return 0;
    if (deg >= wipe_degrees) { // a full turn — the whole row, in one span
        out[0] = .{ .c0 = 0, .c1 = cols };
        return 1;
    }
    var n: usize = 0;
    var open = false;
    var start: u16 = 0;
    var c: u16 = 0;
    while (c < cols) : (c += 1) {
        // Cell centres: column `c` is the (c+1)-th cell, so its centre sits at c + 1.
        const passed = cellAngle(@floatFromInt(row), @as(f64, @floatFromInt(c)) + 1.0, cy, cx) < deg;
        if (passed and !open) {
            open = true;
            start = c;
        } else if (!passed and open) {
            open = false;
            out[n] = .{ .c0 = start, .c1 = c };
            n += 1;
            if (n == out.len) return n;
        }
    }
    if (open) {
        out[n] = .{ .c0 = start, .c1 = cols };
        n += 1;
    }
    return n;
}

/// A batched terminal frame: rows are accumulated and written in one go, so a whole animation

// How far round the clock hand has come: one full turn over the seam's whole distance,
// taken `icon_speed` times as fast, clamped at a full turn (past that it stays home).
pub fn iconDegrees(self: *Screen, x: u16) f64 {
    if (self.wipe_reach == 0 or x >= self.wipe_reach) return wipe_degrees;
    const travelled = @as(f64, @floatFromInt(x)) / @as(f64, @floatFromInt(self.wipe_reach));
    return @min(wipe_degrees, wipe_degrees * travelled * icon_speed);
}

// Which columns of a header row are already in the new accent: inside the icon's own
// columns the CLOCK (`deg` into its turn) decides, past them the left-to-right seam does.
pub fn iconSpans(self: *Screen, row: u16, deg: f64, x: u16, out: *[3]Span) usize {
    const w = @min(icon_cols, self.cols);
    var icon: [2]Span = undefined;
    const cy = (@as(f64, @floatFromInt(self.headerRows())) + 1.0) / 2.0;
    const cx = (@as(f64, @floatFromInt(w)) + 1.0) / 2.0;
    var n = sweptSpansAt(row, w, cy, cx, deg, &icon);
    @memcpy(out[0..n], icon[0..n]);
    if (x > w) { // the seam, on the columns to the right of the icon
        out[n] = .{ .c0 = w, .c1 = @min(x, self.cols) };
        n += 1;
    }
    return n;
}

const testing = std.testing;

test "cellAngle: a clock face — up is 0, then right, down, left" {
    // Pivot at row 10, col 10 on a 19x19 field. Rows count double (cell_aspect), so a cell one row up
    // is as far as two columns across.
    try testing.expectApproxEqAbs(@as(f64, 0), cellAngle(9, 10, 10, 10), 0.001); // straight up
    try testing.expectApproxEqAbs(@as(f64, 90), cellAngle(10, 11, 10, 10), 0.001); // right
    try testing.expectApproxEqAbs(@as(f64, 180), cellAngle(11, 10, 10, 10), 0.001); // down
    try testing.expectApproxEqAbs(@as(f64, 270), cellAngle(10, 9, 10, 10), 0.001); // left
    // One row up and two columns right = 45° once the aspect is applied (2 rows' worth up).
    try testing.expectApproxEqAbs(@as(f64, 45), cellAngle(9, 12, 10, 10), 0.001);
    // The pivot itself reads 0, so the very first frame already covers it.
    try testing.expectEqual(@as(f64, 0), cellAngle(10, 10, 10, 10));
    // Angles are always given going clockwise, never negative.
    try testing.expect(cellAngle(9, 8, 10, 10) > 270);
}
test "sweptSpansAt: the hand covers a row a quadrant at a time, and wraps at the end" {
    var sp: [2]Span = undefined;
    // Row 5 of a 11-wide field with the pivot at (5, 6): the hand's own row.
    // Nothing at 0°, everything after a full turn.
    try testing.expectEqual(@as(usize, 0), sweptSpansAt(5, 11, 5, 6, 0, &sp));
    try testing.expectEqual(@as(usize, 1), sweptSpansAt(5, 11, 5, 6, 360, &sp));
    try testing.expectEqual(Span{ .c0 = 0, .c1 = 11 }, sp[0]);

    // At 90° the hand has swept the top-right quadrant, so on the pivot's row everything from
    // the pivot rightwards is lit and nothing to its left is.
    var n = sweptSpansAt(5, 11, 5, 6, 90.001, &sp);
    try testing.expectEqual(@as(usize, 1), n);
    try testing.expectEqual(@as(u16, 5), sp[0].c0); // 0-based column of the pivot (col 6)
    try testing.expectEqual(@as(u16, 11), sp[0].c1);

    // Half past: the right side of a row BELOW the pivot has been passed, the left has not.
    n = sweptSpansAt(7, 11, 5, 6, 180, &sp);
    try testing.expectEqual(@as(usize, 1), n);
    try testing.expect(sp[0].c0 >= 5 and sp[0].c1 == 11);

    // On the last quarter the sector wraps back over 12 o'clock: a row ABOVE the pivot is lit at both
    // ends and dark between. How far round that takes depends on the row.
    n = sweptSpansAt(3, 11, 5, 6, 330, &sp);
    try testing.expectEqual(@as(usize, 2), n);
    try testing.expectEqual(@as(u16, 0), sp[0].c0); // the far left, just passed
    try testing.expect(sp[0].c1 < sp[1].c0); // …with unswept cells between the two
    try testing.expectEqual(@as(u16, 11), sp[1].c1);

    // A zero-width screen has nothing to sweep.
    try testing.expectEqual(@as(usize, 0), sweptSpansAt(5, 0, 5, 6, 180, &sp));
}
test "iconSpans: the clock turns inside the icon, the seam owns everything past it" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var s = Screen{ .gpa = a, .io = threaded.io(), .fd = -1, .rows = 24, .cols = 80 };
    defer s.freeAllForTest();
    for (0..10) |_| try s.header.append(a, try a.dupe(u8, "logo row"));
    s.wipe_reach = 40;

    var sp: [3]Span = undefined;
    // Nothing has moved yet: no clock, no seam.
    try testing.expectEqual(@as(usize, 0), iconSpans(&s, 1, 0, 0, &sp));
    // Mid-animation the seam is past the icon, so a header row carries the icon's swept stretch AND
    // the seam's — and the seam's never starts before the icon's columns end.
    const n = iconSpans(&s, 5, 90.0, 30, &sp);
    try testing.expect(n >= 1);
    const seam = sp[n - 1];
    try testing.expectEqual(@as(u16, icon_cols), seam.c0);
    try testing.expectEqual(@as(u16, 30), seam.c1);
    for (sp[0 .. n - 1]) |icon| try testing.expect(icon.c1 <= icon_cols); // the clock stays inside
    // While the seam is still crossing the icon there is no seam stretch at all — those columns
    // belong to the hand.
    const m = iconSpans(&s, 5, 45.0, icon_cols - 4, &sp);
    for (sp[0..m]) |v| try testing.expect(v.c1 <= icon_cols);
    // Settled: a full turn covers the icon in one stretch, and the seam covers the rest.
    const k = iconSpans(&s, 5, wipe_degrees, s.cols, &sp);
    try testing.expectEqual(@as(usize, 2), k);
    try testing.expectEqual(Span{ .c0 = 0, .c1 = icon_cols }, sp[0]);
    try testing.expectEqual(Span{ .c0 = icon_cols, .c1 = 80 }, sp[1]);
}
test "iconDegrees: one full turn over exactly the distance the seam travels" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var s = Screen{ .gpa = a, .io = threaded.io(), .fd = -1, .rows = 24, .cols = 80 };
    defer s.freeAllForTest();
    s.wipe_reach = 40;
    try testing.expectEqual(@as(f64, 0), iconDegrees(&s, 0)); // 12 o'clock, nothing swept
    // 1.25× the seam: half its travel has the hand a quarter past half way round.
    try testing.expectEqual(@as(f64, 225), iconDegrees(&s, 20));
    // …so the turn is finished at 80% of the travel, and stays finished after that.
    try testing.expectEqual(wipe_degrees, iconDegrees(&s, 32));
    try testing.expectEqual(wipe_degrees, iconDegrees(&s, 36));
    try testing.expectEqual(wipe_degrees, iconDegrees(&s, 40)); // the seam arrives, the hand is home
    try testing.expectEqual(wipe_degrees, iconDegrees(&s, 999)); // the settle frame, past the end
    // The hand never runs backwards or overshoots into a second turn.
    var prev: f64 = 0;
    var x: u16 = 0;
    while (x <= 40) : (x += 1) {
        const d = iconDegrees(&s, x);
        try testing.expect(d >= prev and d <= wipe_degrees);
        prev = d;
    }
    s.wipe_reach = 0; // nothing to recolour at all — no partial turn left behind
    try testing.expectEqual(wipe_degrees, iconDegrees(&s, 0));
}

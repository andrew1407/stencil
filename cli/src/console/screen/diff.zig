//! Painting only what changed: each screen row's last paint is remembered as a hash, so a plain
//! paint skips the rows that already show their bytes, and new output slides the body up in a
//! scroll region instead of redrawing it. Under a secret skin every row repaints as it always has.
const std = @import("std");
const sc = @import("../screen.zig");
const Screen = sc.Screen;
const skin = @import("../../app/skin.zig");
const ttyWrite = sc.ttyWrite;

/// Whether `row` already shows `bytes`, by the hash of its last paint from here — recorded on every
/// paint, honoured only while `skip_unchanged` (an animated skin's frame; see `Screen.row_hashes`).
pub fn rowUnchanged(self: *Screen, row: u16, bytes: []const u8) bool {
    if (row == 0 or row > self.row_hashes.len) return false;
    const slot = &self.row_hashes[row - 1];
    const h = @max(std.hash.Wyhash.hash(0, bytes), 1); // 0 stays "unknown"
    defer slot.* = h;
    return self.skip_unchanged and slot.* == h;
}

/// Whether paints may skip rows and scroll: the console's own look. A secret skin repaints
/// every row as it always has, so its effects keep their exact frames.
pub fn plain() bool {
    return skin.get() == .none;
}

/// The body and the rules, writing only the rows whose bytes differ from their last paint.
pub fn paintChanged(self: *Screen) void {
    self.skip_unchanged = true;
    defer self.skip_unchanged = false;
    self.paintBody();
    self.drawStatusBar();
}

/// Forget what rows `from`…`to` (1-based, inclusive) show, so the next paint writes them whatever
/// they hold — rows something other than a paint wrote over (the input block moving).
pub fn forget(self: *Screen, from: u16, to: u16) void {
    const lo: usize = @max(from, 1);
    const hi: usize = @min(to, self.row_hashes.len);
    if (lo <= hi) @memset(self.row_hashes[lo - 1 .. hi], 0);
}

/// Slide the body's rows `n` up (new output below; negative = down, scrolling back) inside a
/// scroll region, each row's remembered paint moving with it; the rows it opens are unknown.
pub fn shiftBody(self: *Screen, n: isize) void {
    const top: usize = self.bodyTop();
    const bottom: usize = self.statusRow() -| 1;
    if (bottom < top or bottom > self.row_hashes.len) return;
    const k: usize = @abs(n);
    if (k == 0 or k >= bottom - top + 1) return;
    var b: [48]u8 = undefined;
    const seq = std.fmt.bufPrint(&b, "\x1b[{d};{d}r\x1b[{d}{c}\x1b[r", .{ top, bottom, k, @as(u8, if (n > 0) 'S' else 'T') }) catch return;
    ttyWrite(self.fd, seq);
    const rows = self.row_hashes[top - 1 .. bottom];
    if (n > 0) {
        std.mem.copyForwards(u64, rows[0 .. rows.len - k], rows[k..]);
        @memset(rows[rows.len - k ..], 0);
    } else {
        std.mem.copyBackwards(u64, rows[k..], rows[0 .. rows.len - k]);
        @memset(rows[0..k], 0);
    }
}

const testing = std.testing;

test "an animated frame skips a row that already shows the same bytes; a full paint forgets them all" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var s = Screen{ .gpa = a, .io = threaded.io(), .fd = -1, .rows = 10, .cols = 40 };
    defer s.freeAll();
    try testing.expect(!rowUnchanged(&s, 3, "abc")); // first sight of the row
    try testing.expect(!rowUnchanged(&s, 3, "abc")); // the same again, but not inside a skin frame
    s.skip_unchanged = true;
    try testing.expect(rowUnchanged(&s, 3, "abc"));
    try testing.expect(!rowUnchanged(&s, 3, "abd")); // a change goes out …
    try testing.expect(rowUnchanged(&s, 3, "abd")); // … and is then what the row shows
    try testing.expect(!rowUnchanged(&s, 0, "") and !rowUnchanged(&s, sc.max_cached_rows + 1, "")); // never cached
    try testing.expect(!rowUnchanged(&s, 9, "abd") and rowUnchanged(&s, 9, "abd")); // the prompt row: no paint here touches it
    s.fullPaint(); // clears the terminal, so every row is unknown again — then paints the body and the rules
    try testing.expect(!rowUnchanged(&s, 9, "abd"));
    try testing.expect(rowUnchanged(&s, 3, "")); // the gap clear left this body row blank, and it still is
}

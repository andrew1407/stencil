//! The matrix's entrance: a moment of green digit rain down the output rows before the
//! console repaints over it. A keystroke cuts it short.
const std = @import("std");
const screen_mod = @import("../../screen.zig");
const timing = @import("timing.zig");
const Screen = screen_mod.Screen;

const frames = 90;
const frame_ms = 30;
const trail = 14; // rows a drop's digits stay lit behind its head
const max_cols = 512;
const head_sgr = "\x1b[1;38;2;150;245;200;48;2;0;0;0m";
const body_sgr = "\x1b[0;38;2;35;209;139;48;2;0;0;0m";

pub fn rain(self: *Screen) void {
    const top: i32 = self.bodyTop();
    const bottom: i32 = @as(i32, self.statusRow()) - 1;
    if (bottom < top) return;
    const h = bottom - top + 1;
    const cols: usize = @min(self.cols, max_cols);
    var prng = std.Random.DefaultPrng.init(@truncate(@as(u96, @bitCast(std.Io.Clock.now(.awake, self.io).nanoseconds))));
    const rnd = prng.random();
    var head: [max_cols]i32 = undefined;
    for (head[0..cols]) |*hd| hd.* = -rnd.intRangeLessThan(i32, 0, h + trail);

    var f: usize = 0;
    while (f < frames) : (f += 1) {
        var frame = timing.Frame{ .fd = self.fd };
        var c: usize = 0;
        while (c < cols) : (c += 1) {
            head[c] += 1;
            if (head[c] - trail > h) head[c] = -rnd.intRangeLessThan(i32, 0, h); // a fresh drop falls
            const r = head[c];
            put(&frame, top, h, r, c, head_sgr, digit(rnd));
            put(&frame, top, h, r - 1, c, body_sgr, digit(rnd));
            put(&frame, top, h, r - trail, c, body_sgr, " ");
        }
        frame.flush();
        if (timing.waitFrame(self, frame_ms)) break;
    }
}

fn digit(rnd: std.Random) []const u8 {
    return if (rnd.boolean()) "1" else "0";
}

// One cell of the rain, skipped when `r` (0-based below `top`) is off the output rows.
fn put(frame: *timing.Frame, top: i32, h: i32, r: i32, c: usize, sgr: []const u8, glyph: []const u8) void {
    if (r < 0 or r >= h) return;
    var b: [24]u8 = undefined;
    frame.put(std.fmt.bufPrint(&b, "\x1b[{d};{d}H", .{ top + r, c + 1 }) catch return);
    frame.put(sgr);
    frame.put(glyph);
}

test "rain: a screen with no output rows draws nothing" {
    var threaded = std.Io.Threaded.init(std.testing.allocator, .{});
    defer threaded.deinit();
    var s = Screen{ .gpa = std.testing.allocator, .io = threaded.io(), .fd = -1, .rows = 1, .cols = 10 };
    defer s.freeAll();
    rain(&s);
}

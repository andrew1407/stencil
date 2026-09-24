//! The reveal sweep: new output types in from the left, jump by jump. A BURST — output
//! arriving before the quiet window lapses — sweeps in fewer, larger jumps, so a flood of
//! lines still lands promptly.
const std = @import("std");
const logo = @import("../../../app/logo.zig");
const ansi = @import("../ansi.zig");
const screen_mod = @import("../../screen.zig");
const Screen = screen_mod.Screen;
const Span = ansi.Span;
const timing = @import("timing.zig");
const skin = @import("../../../app/skin.zig");

const waitFrame = timing.waitFrame;
const Frame = timing.Frame;

// New output sweeps in from the left, jump-by-jump like the recolour wipe, but much
// quicker — it plays on EVERY line, so the total stays ~0.1s and queued keystrokes skip it.
const reveal_step_ms = 13;
const reveal_jumps = 8;
// A BURST — output arriving with under `reveal_quiet_ms` of silence — gets the short form; past
// `reveal_burst_max_ms` of continuous output the sweep gives up and the rest lands at once.
const reveal_burst_jumps = 2;
const reveal_quiet_ms = 250;
const reveal_burst_max_ms = 900;

// However slow the speed, no burst animates for longer than this — output still has to arrive.
const reveal_burst_ceiling_ms = 5000;

// Whether new output should be swept in rather than printed at once: not turned off (speed 1 = no
// waiting) and there is a real terminal to animate on (tests drive a Screen with no fd).
pub fn revealing(self: *Screen) bool {
    return self.reveal_speed < screen_mod.speed_max and self.fd >= 0;
}

// The pace one sweep runs at, in ms. All three windows scale together off the speed, so only the
// tempo changes with the setting; `factor` is 1 at the default speed of 0.5.
const Pace = struct { step_ms: i64, quiet_ms: i64, burst_max_ms: i64 };

fn pace(self: *Screen) Pace {
    const s = std.math.clamp(self.reveal_speed, screen_mod.speed_min, screen_mod.speed_max);
    const factor = (1.0 - s) / s;
    const scale = struct {
        fn f(base: comptime_int, k: f64, floor: i64, ceil: i64) i64 {
            const v: f64 = @round(@as(f64, base) * k);
            if (!(v > @as(f64, @floatFromInt(floor)))) return floor;
            const n: i64 = @intFromFloat(@min(v, @as(f64, @floatFromInt(ceil))));
            return n;
        }
    }.f;
    return .{
        .step_ms = scale(reveal_step_ms, factor, 1, reveal_burst_ceiling_ms),
        // The quiet window must outlast one line's own sweep, or every line of a burst
        // would look like the first one after a pause and get the long form again.
        .quiet_ms = scale(reveal_quiet_ms, factor, reveal_quiet_ms, reveal_burst_ceiling_ms),
        .burst_max_ms = scale(reveal_burst_max_ms, factor, reveal_burst_max_ms, reveal_burst_ceiling_ms),
    };
}

pub fn revealNew(self: *Screen, n_new: usize) void {
    const ns_ms = std.time.ns_per_ms;
    const p = pace(self);
    const now = std.Io.Clock.now(.awake, self.io).nanoseconds;
    // Still inside a burst, or the first line after a pause?
    const in_burst = self.reveal_last_ns != 0 and now - self.reveal_last_ns <= p.quiet_ms * ns_ms;
    if (!in_burst) self.reveal_burst_ns = now;
    defer self.reveal_last_ns = std.Io.Clock.now(.awake, self.io).nanoseconds;
    if (in_burst and now - self.reveal_burst_ns > p.burst_max_ms * ns_ms) {
        self.paintBody(); // output has been pouring for a while — stop animating it
        return;
    }
    const jumps: u16 = if (in_burst) reveal_burst_jumps else reveal_jumps;
    const first_new = self.lines.items.len - n_new;
    // How far right the new text actually reaches — sweeping past that is dead time.
    var reach: u16 = 0;
    const w = self.window();
    var i: usize = @max(w.first, first_new);
    while (i < w.end) : (i += 1) {
        const vis: u16 = @intCast(@min(ansi.visColumns(self.lines.items[i]), @as(usize, self.cols)));
        reach = @max(reach, vis);
    }
    if (reach == 0) { // nothing visible arrived (blank lines, or all of it scrolled off)
        self.paintBody();
        return;
    }
    const per: u16 = @max(1, (reach + jumps - 1) / jumps); // columns per jump
    self.paintBodyCut(first_new, 0); // the settled rows, with the new ones still blank
    var x: u16 = per;
    while (x < reach) : (x +|= per) {
        if (waitFrame(self, p.step_ms)) break;
        if (!skinFrame(self, first_new, x)) paintNewRows(self, first_new, x);
    }
    self.paintBody(); // settle: the whole text, however the loop ended
}

/// Move an animated skin to the clock's frame and repaint what it colours: the logo, the rule
/// and the body — with rows from `first_new` cut at `x` while a sweep is typing them in. False
/// when no skin moved, so nothing was painted.
pub fn skinFrame(self: *Screen, first_new: ?usize, x: u16) bool {
    const s = skin.get();
    const t = skin.traitsOf(s);
    if (!t.animated) return false;
    if (!skin.syncClock(std.Io.Clock.now(.awake, self.io).toMilliseconds())) return false;
    if (s == .fairylight) { // the next bulb lights with the very wipe a `/theme` change runs
        if (first_new != null) return false;
        const rgb = skin.nextBulb() orelse return false;
        logo.setAccent(rgb);
        self.onThemeChanged();
        return true;
    }
    if (t.moves_logo) self.captureHeader();
    self.skip_unchanged = true; // most frames move a picture or two: only those rows go out
    defer self.skip_unchanged = false;
    self.paintHeader();
    self.paintBodyCut(first_new, x);
    self.drawStatusBar();
    return true;
}

// One frame of the sweep: only the arriving rows, each cut to its first `x` visible
// columns. Batched into a single write so a frame lands as one terminal update.
fn paintNewRows(self: *Screen, first_new: usize, x: u16) void {
    if (self.bodyRows() == 0) return;
    var rb: [8192]u8 = undefined;
    var frame = Frame{ .fd = self.fd };
    const w = self.window();
    var r: u16 = self.headerRows() + 1;
    var i: usize = w.first;
    while (i < w.end) : (i += 1) {
        if (i >= first_new) {
            frame.at(r);
            frame.put(ansi.clipPrefix(self.lines.items[i], self.cols, x, &rb));
            frame.put("\x1b[K"); // erase what the sweep has not reached yet
        }
        r += 1;
    }
    frame.flush();
}

const testing = std.testing;

test "reveal speed: 0.5 by default, 1 means instantly, and no terminal never animates" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var s = Screen{ .gpa = a, .io = threaded.io(), .fd = -1, .rows = 10, .cols = 40 };
    defer s.freeAllForTest();
    try testing.expectEqual(@as(f64, 0.5), s.revealSpeed()); // the default the console starts with
    // No fd = no terminal (tests, a captured sink): the sweep must not run, so a suite
    // never sits through an animation nobody can see.
    try testing.expect(!revealing(&s));
    s.fd = 1;
    try testing.expect(revealing(&s));
    s.setRevealSpeed(1.0); // '/reveal 1' — text is simply there, no waiting at all
    try testing.expect(!revealing(&s));
    s.setRevealSpeed(screen_mod.speed_min);
    try testing.expect(revealing(&s));
    // Out of range is clamped rather than refused — the command validates, this is the floor.
    s.setRevealSpeed(0.0);
    try testing.expectEqual(screen_mod.speed_min, s.revealSpeed());
    s.setRevealSpeed(4.0);
    try testing.expectEqual(screen_mod.speed_max, s.revealSpeed());
    s.fd = -1;
}
test "reveal speed: the pace scales off it, and is 1x at the default" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var s = Screen{ .gpa = a, .io = threaded.io(), .fd = -1, .rows = 10, .cols = 40 };
    defer s.freeAllForTest();
    // 0.5 → factor 1: exactly the pace the sweep had before the speed setting existed.
    const mid = pace(&s);
    try testing.expectEqual(@as(i64, reveal_step_ms), mid.step_ms);
    try testing.expectEqual(@as(i64, reveal_quiet_ms), mid.quiet_ms);
    try testing.expectEqual(@as(i64, reveal_burst_max_ms), mid.burst_max_ms);
    // Slower speed = longer frames; the burst windows grow with them so a slow line's own
    // sweep can't be mistaken for silence, and nothing exceeds the ceiling.
    s.setRevealSpeed(screen_mod.speed_min);
    const slow = pace(&s);
    try testing.expect(slow.step_ms > mid.step_ms);
    try testing.expect(slow.quiet_ms >= slow.step_ms);
    try testing.expect(slow.burst_max_ms <= reveal_burst_ceiling_ms);
    // Faster speed = shorter frames, never zero (a zero-length frame is not an animation).
    s.setRevealSpeed(0.95);
    const fast = pace(&s);
    try testing.expect(fast.step_ms >= 1 and fast.step_ms < mid.step_ms);
    // The windows never shrink below their base, or a fast setting would give up on a burst
    // sooner than one line takes to arrive.
    try testing.expect(fast.quiet_ms >= reveal_quiet_ms and fast.burst_max_ms >= reveal_burst_max_ms);
}
test "reveal pacing: a burst sweeps shorter than a lone line, and is bounded" {
    // A line arriving on its own gets the full sweep; lines pouring in get the short form,
    // or twenty prints in a row would each queue an animation behind the last.
    try testing.expect(reveal_burst_jumps >= 1 and reveal_burst_jumps < reveal_jumps);
    // However long the output runs, the animating stops after this — the cap has to be
    // longer than one full sweep, or a single line would already exceed it.
    try testing.expect(reveal_burst_max_ms > reveal_jumps * reveal_step_ms);
}

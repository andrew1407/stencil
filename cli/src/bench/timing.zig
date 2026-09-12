//! Timing plumbing for the opt-in bench (`zig build bench`) — the Zig counterpart of core's
//! tests/benchSupport.hpp. Best-of-N drops scheduler noise; the numbers are a drift
//! reference, so nothing here compares one against a wall-clock threshold.
const std = @import("std");

/// Batches per measurement, as core's `best_ms` and mcp's `per_call` use.
pub const reps = 5;

/// Elapsed monotonic milliseconds since `t0` (Zig 0.16 clocks live on std.Io).
pub fn elapsedMs(io: std.Io, t0: std.Io.Timestamp) f64 {
    const ns = t0.durationTo(std.Io.Clock.awake.now(io)).toNanoseconds();
    return @as(f64, @floatFromInt(ns)) / 1e6;
}

/// Best (min) wall-clock over `n` runs, in milliseconds — drops scheduler noise.
pub fn bestMs(io: std.Io, n: usize, ctx: anytype, comptime run: fn (@TypeOf(ctx)) void) f64 {
    var best: f64 = std.math.floatMax(f64);
    var i: usize = 0;
    while (i < n) : (i += 1) {
        const t0 = std.Io.Clock.awake.now(io);
        run(ctx);
        const ms = elapsedMs(io, t0);
        if (ms < best) best = ms;
    }
    return best;
}

/// Best per-call cost in MICROSECONDS over `reps` batches of `rounds` calls, printed and
/// handed back. One warm round first: several of these paths parse an embedded asset lazily.
pub fn perCallUs(io: std.Io, label: []const u8, rounds: usize, ctx: anytype, comptime run: fn (@TypeOf(ctx)) void) f64 {
    run(ctx);
    var best: f64 = std.math.floatMax(f64);
    var i: usize = 0;
    while (i < reps) : (i += 1) {
        const t0 = std.Io.Clock.awake.now(io);
        var k: usize = 0;
        while (k < rounds) : (k += 1) run(ctx);
        const ms = elapsedMs(io, t0);
        if (ms < best) best = ms;
    }
    const us = best * 1000 / @as(f64, @floatFromInt(rounds));
    std.debug.print("  {s: <38}{d:>9.2} us/call  (best of {d} x {d})\n", .{ label, us, reps, rounds });
    return us;
}

/// The tally behind the only assertions a bench run makes. A ratio between two sizes of the
/// SAME call catches an algorithmic regression; a wall-clock threshold would just catch a
/// busy machine. Failures are counted, not thrown, so one regression still prints the rest.
pub const Ratios = struct {
    failures: usize = 0,

    pub fn under(self: *Ratios, label: []const u8, big: f64, small: f64, ceiling: f64, why: []const u8) void {
        const got = big / small;
        std.debug.print("  -> {s}: {d:.2}x (ceiling {d:.0}x) — {s}\n", .{ label, got, ceiling, why });
        if (got >= ceiling) {
            std.debug.print("  REGRESSED: {s} cost {d:.2}x, over its {d:.0}x ceiling\n", .{ label, got, ceiling });
            self.failures += 1;
        }
    }
};

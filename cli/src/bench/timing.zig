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

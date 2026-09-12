//! Whole-pipeline performance benchmark — the adapter-level counterpart to
//! core/tests/bench.test.cpp, which times the same transforms in isolation. `bench/raster.zig`
//! times the stages pipeline.run composes AS the CLI drives them plus the codec encode the
//! core never sees; `bench/adapters.zig` times the paths off the raster road and asserts how
//! each SCALES between two input sizes — never a wall-clock threshold, so a loaded machine
//! cannot fail a run. Opt-in and hermetic (no files, no network) — NOT in `zig build test`.
//!     zig build bench                     # default 4000x3000, 3000 lines
//!     zig build bench -- 6000 4000 8000   # width height line-count
const std = @import("std");
const adapters = @import("bench/adapters.zig");
const raster = @import("bench/raster.zig");
const timing = @import("bench/timing.zig");

/// `argv` excludes the program name (just the `--`-forwarded args): width height lines.
fn parseArgs(argv: []const []const u8) raster.Dims {
    var d = raster.Dims{ .w = 4000, .h = 3000, .lines = 3000 };
    if (argv.len > 0) d.w = std.fmt.parseInt(usize, argv[0], 10) catch d.w;
    if (argv.len > 1) d.h = std.fmt.parseInt(usize, argv[1], 10) catch d.h;
    if (argv.len > 2) d.lines = std.fmt.parseInt(usize, argv[2], 10) catch d.lines;
    return d;
}

pub fn main(init: std.process.Init) !void {
    const gpa = init.gpa;
    const arena = init.arena.allocator();
    const argv = try init.minimal.args.toSlice(arena);

    const d = parseArgs(argv[1..]);
    const mp = @as(f64, @floatFromInt(d.w * d.h)) / 1e6;
    std.debug.print("stencil CLI pipeline bench — {d}x{d} ({d:.1} MP), {d} lines\n", .{ d.w, d.h, mp, d.lines });

    try raster.run(gpa, init.io, d);

    var ratios = timing.Ratios{};
    try adapters.run(gpa, init.io, &ratios);
    if (ratios.failures != 0) {
        std.debug.print("\n{d} scaling ratio(s) over ceiling\n", .{ratios.failures});
        return error.BenchRegressed;
    }
}

// Registration only (tests/test_registration_test.zig): these reach the `zig build bench`
// exe, not the test build, so a compile error here surfaces on a bench run.
test {
    _ = @import("bench/adapters.zig");
    _ = @import("bench/fixtures.zig");
    _ = @import("bench/raster.zig");
    _ = @import("bench/timing.zig");
}

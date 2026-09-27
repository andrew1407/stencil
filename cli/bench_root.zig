// Bench entry point for `zig build bench`. Rooted at cli/, as test_root.zig is, so the
// benchmarks under src/bench/ may import the app modules beside them in src/.
pub const main = @import("src/bench/bench.zig").main;

//! stencil — a small command-line image tool wrapping the shared C++ core.
//! Usage and flags: see logo.usage() (or run with --help).
const std = @import("std");
const args = @import("args.zig");
const pipeline = @import("pipeline.zig");
const logo = @import("logo.zig");

pub fn main(init: std.process.Init) !void {
    logo.init(init.environ_map.getPtr("NO_COLOR") != null);
    const gpa = init.gpa;
    const io = init.io;
    const arena = init.arena.allocator();

    const argv = try init.minimal.args.toSlice(arena);
    const cli_args = argv[1..];

    const opts = args.parse(gpa, cli_args) catch {
        logo.banner();
        logo.usage();
        std.process.exit(2);
    };

    if (opts.help or cli_args.len == 0) {
        logo.banner();
        logo.usage();
        return;
    }

    pipeline.run(gpa, io, opts) catch {
        // pipeline.run prints a human-readable reason before failing.
        std.process.exit(1);
    };
}

test {
    // Pull every module into the test build so their `test` blocks run.
    _ = @import("args.zig");
    _ = @import("core.zig");
    _ = @import("image.zig");
    _ = @import("layout.zig");
    _ = @import("video.zig");
    _ = @import("net.zig");
    _ = @import("pipeline.zig");
}

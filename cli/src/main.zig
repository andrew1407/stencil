//! stencil — a small command-line image tool wrapping the shared C++ core.
//! Usage and flags: see logo.usage() (or run with --help).
const std = @import("std");
const args = @import("args.zig");
const pipeline = @import("pipeline.zig");
const console = @import("console.zig");
const scrape = @import("scrape.zig");
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

    if (opts.console) {
        console.run(gpa, io) catch {
            std.process.exit(1);
        };
        return;
    }

    // Scrape mode: --source-site fetches a page, extracts + filters media, and downloads the
    // matches into <output> (a directory). It ignores the editing/connection flags.
    if (opts.source_site != null) {
        scrape.run(gpa, io, opts) catch {
            // scrape.run prints a human-readable reason before failing.
            std.process.exit(1);
        };
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
    _ = @import("scrape.zig");
    _ = @import("serverClient.zig");
    _ = @import("pipeline.zig");
    _ = @import("console.zig");
    _ = @import("theme.zig");
    _ = @import("line_edit.zig");
    _ = @import("clipboard.zig");
}

//! stencil — a small command-line image tool wrapping the shared C++ core.
//! Usage and flags: see logo.usage() (or run with --help).
const std = @import("std");
const args = @import("args.zig");
const pipeline = @import("pipeline.zig");
const console = @import("console.zig");
const scrape = @import("scrape.zig");
const script = @import("script.zig");
const project = @import("project.zig");
const project_cli = @import("project_cli.zig");
const llm = @import("llm.zig");
const logo = @import("logo.zig");
const child = @import("child.zig");

pub fn main(init: std.process.Init) !void {
    // Colour off when NO_COLOR is set; the severity prefixes also need stderr (the human
    // channel) to be a terminal, so a redirected/piped run stays plain text.
    logo.init(
        init.environ_map.getPtr("NO_COLOR") != null,
        std.Io.File.stderr().isTty(init.io) catch false,
    );
    // Children (ffmpeg, clipboard helpers) must not inherit the user's LLM key.
    child.captureEnv(init.environ_map);
    const gpa = init.gpa;
    const io = init.io;
    const arena = init.arena.allocator();

    const argv = try init.minimal.args.toSlice(arena);
    const cli_args = argv[1..];

    const opts = args.parse(cli_args) catch {
        logo.banner();
        logo.usage();
        std.process.exit(2);
    };

    switch (args.modeOf(opts, cli_args.len, project.isStencilPath)) {
        .usage => {
            logo.banner();
            logo.usage();
            return;
        },
        // The console's /prompt + /llm commands seed their provider config from the
        // STENCIL_LLM_* environment (llm-contract.md §5).
        .console => |c| return console.run(gpa, io, c.full_screen, llm.Env.fromMap(init.environ_map)) catch
            std.process.exit(1),
        // Scrape mode: --source-site fetches a page, extracts + filters media, and downloads
        // the matches into <output> (a directory). scrape.run prints its own reason.
        .scrape => return scrape.run(gpa, io, opts) catch std.process.exit(1),
        // Script mode: a .stc drives the edits. `check` and `plan` report on stdout and
        // change nothing; `run` does the work. Each prints its own reason and exits 1. Only
        // this layer opens a terminal, so the two stdout modes are handed the writer.
        .script => |sc| {
            var buf: [4096]u8 = undefined;
            var stdout = std.Io.File.stdout().writerStreaming(io, &buf);
            const out = &stdout.interface;
            switch (sc.kind) {
                .run => return script.run.run(gpa, io, opts, sc.path) catch std.process.exit(1),
                .check => return script.check.run(gpa, io, out, sc.path) catch std.process.exit(1),
                .plan => return script.plan.run(gpa, io, out, opts, sc.path) catch std.process.exit(1),
            }
        },
        // A `.stencil` project on either side reuses the console Session so its layout renders
        // like the editors; server mode stays on the raster pipeline.
        .project => return project_cli.runOneShot(gpa, io, opts) catch std.process.exit(1),
        .pipeline => {},
    }

    pipeline.run(gpa, io, opts) catch {
        // pipeline.run prints a human-readable reason before failing.
        std.process.exit(1);
    };
}

test {
    // Pull every module into the test build so their `test` blocks run.
    _ = @import("logo.zig");
    _ = @import("args.zig");
    _ = @import("core.zig");
    _ = @import("scriptCore.zig");
    _ = @import("script.zig");
    _ = @import("image.zig");
    _ = @import("layout.zig");
    _ = @import("video.zig");
    _ = @import("net.zig");
    _ = @import("llm.zig");
    _ = @import("scrape.zig");
    _ = @import("serverClient.zig");
    _ = @import("pipeline.zig");
    _ = @import("project.zig");
    _ = @import("project_cli.zig");
    _ = @import("console.zig");
    _ = @import("theme.zig");
    _ = @import("line_edit.zig");
    _ = @import("clipboard.zig");
    _ = @import("child.zig");
    _ = @import("confine.zig");
    _ = @import("sanitize.zig");
    _ = @import("page.zig");
    _ = @import("brand.zig");
    _ = @import("host.zig");
    _ = @import("messages.zig");
    _ = @import("mediaTypes.zig");
    _ = @import("fetchPool.zig");
    _ = @import("imageRows.zig");
    _ = @import("report.zig");
}

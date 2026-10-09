//! stencil — a small command-line image tool wrapping the shared C++ core.
//! Usage and flags: see logo.usage() (or run with --help).
const std = @import("std");
const builtin = @import("builtin");
const args = @import("args.zig");
const pipeline = @import("pipeline.zig");
const console = @import("console.zig");
const scrape = @import("scrape.zig");
const script = @import("script.zig");
const inspect = @import("inspect.zig");
const project = @import("project.zig");
const project_cli = @import("project/cli.zig");
const llm = @import("llm.zig");
const logo = @import("app/logo.zig");
const report = @import("app/report.zig");
const child = @import("safety/child.zig");
const terminal = @import("console/screen/terminal.zig");

/// A panic hands the terminal back before it reports: raw mode, the alternate screen and
/// mouse tracking would otherwise outlive the process.
pub const panic = std.debug.FullPanic(panicRestoring);

fn panicRestoring(msg: []const u8, first_trace_addr: ?usize) noreturn {
    // The console, and so a held terminal, exists only on POSIX.
    if (builtin.os.tag != .windows) terminal.restoreTerminal();
    std.debug.defaultPanic(msg, first_trace_addr);
}

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

    var opts = args.parse(cli_args) catch {
        logo.banner();
        logo.usage();
        std.process.exit(2);
    };
    opts.env_tokens = .{
        .per_origin = init.environ_map.get("STENCIL_SERVER_TOKENS"),
        .single = init.environ_map.get("STENCIL_SERVER_TOKEN"),
    };

    switch (args.modeOf(opts, cli_args.len, project.isStencilPath)) {
        .usage => {
            logo.banner();
            logo.usage();
            return;
        },
        // The console's /prompt + /llm commands seed their provider config from the
        // STENCIL_LLM_* environment (llm-contract.md §5).
        .console => |c| {
            // The console's raw mode is termios; Windows gets every one-shot mode, not the console.
            if (builtin.os.tag == .windows) {
                report.err("the interactive console is not available on Windows\n", .{});
                std.process.exit(1);
            }
            return console.run(gpa, io, c.full_screen, llm.Env.fromMap(init.environ_map), opts.env_tokens) catch
                std.process.exit(1);
        },
        // Scrape mode: --source-site fetches a page, extracts + filters media, and downloads
        // the matches into <output> (a directory). scrape.run prints its own reason.
        .scrape => return scrape.run(gpa, io, opts) catch std.process.exit(1),
        // Script mode: a .stc drives the edits. `check` and `plan` report on stdout and change nothing, `run`
        // does the work. Only this layer opens a terminal, so the two stdout modes are handed the writer.
        .script => |sc| {
            var buf: [4096]u8 = undefined;
            var stdout = std.Io.File.stdout().writerStreaming(io, &buf);
            const out = &stdout.interface;
            switch (sc.kind) {
                .run => return script.run.run(gpa, io, opts, sc.path) catch std.process.exit(1),
                .check => return script.check.run(gpa, io, out, sc.path) catch std.process.exit(1),
                .plan => return script.plan.run(gpa, io, out, opts, sc.path) catch std.process.exit(1),
                .emit => return script.emit.runGuarded(gpa, io, sc.path, opts.script_emit.?, .{
                    .confine_output = opts.confine_output,
                    .no_clobber = opts.no_clobber,
                }) catch std.process.exit(1),
            }
        },
        // The reporting modes print one JSON document on stdout, handed in from here as for a plan.
        .inspect => |kind| {
            var buf: [4096]u8 = undefined;
            var stdout = std.Io.File.stdout().writerStreaming(io, &buf);
            return inspect.run(gpa, io, &stdout.interface, opts, kind) catch std.process.exit(1);
        },
        // --plan-check prints core's verdict on a model reply: exit 1 for an invalid plan, 2 when
        // the reply or the schema could not be had at all.
        .plan_check => |path| {
            var buf: [4096]u8 = undefined;
            var stdout = std.Io.File.stdout().writerStreaming(io, &buf);
            const status = llm.check.run(gpa, io, &stdout.interface, path, opts.plan_surface orelse "cli", opts.plan_capabilities) catch
                std.process.exit(2);
            if (status == .invalid) std.process.exit(1);
            return;
        },
        // --merge-lines prints the co-edit line union; exit 2 when the input cannot be had or read.
        .merge_lines => |path| {
            var buf: [4096]u8 = undefined;
            var stdout = std.Io.File.stdout().writerStreaming(io, &buf);
            return inspect.mergeLines.run(gpa, io, &stdout.interface, path) catch std.process.exit(2);
        },
        // A `.stencil` project on either side, or a `--prompt` turn, reuses the console Session so
        // its layout renders like the editors; server mode stays on the raster pipeline.
        .project => return project_cli.runWith(gpa, io, opts, llm.Env.fromMap(init.environ_map)) catch std.process.exit(1),
        .pipeline => {},
    }

    pipeline.run(gpa, io, opts) catch {
        // pipeline.run prints a human-readable reason before failing.
        std.process.exit(1);
    };
}

test {
    // Pull every module into the test build so their `test` blocks run.
    _ = @import("app/logo.zig");
    _ = @import("args.zig");
    _ = @import("core.zig");
    _ = @import("script/core.zig");
    _ = @import("core/opplan.zig");
    _ = @import("script.zig");
    _ = @import("inspect.zig");
    _ = @import("media/image.zig");
    _ = @import("media/layout.zig");
    _ = @import("media/video.zig");
    _ = @import("media/remoteVideo.zig");
    _ = @import("net.zig");
    _ = @import("llm.zig");
    _ = @import("scrape.zig");
    _ = @import("server/client.zig");
    _ = @import("pipeline.zig");
    _ = @import("project.zig");
    _ = @import("project/cli.zig");
    _ = @import("console.zig");
    _ = @import("app/theme.zig");
    _ = @import("line_edit/line_edit.zig");
    _ = @import("clipboard.zig");
    _ = @import("safety/child.zig");
    _ = @import("safety/confine.zig");
    _ = @import("safety/input.zig");
    _ = @import("safety/sanitize.zig");
    _ = @import("media/page.zig");
    _ = @import("app/brand.zig");
    _ = @import("net/host.zig");
    _ = @import("app/messages.zig");
    _ = @import("media/types.zig");
    _ = @import("net/fetchPool.zig");
    _ = @import("net/send.zig");
    _ = @import("net/jobCall.zig");
    _ = @import("media/imageRows.zig");
    _ = @import("app/report.zig");
}

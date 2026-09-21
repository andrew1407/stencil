//! What one invocation asks for. The three command modes each own their block —
//! the editing pipeline, the collaboration server, and site scraping — and `Mode` says
//! which one a parsed Options actually selected, so main.zig switches instead of
//! re-deriving the mutual exclusions.
const std = @import("std");
const logo = @import("../app/logo.zig");

pub const Blank = struct {
    page: ?[]const u8 = null, // named page format ("A4".."C10", canonical); null = default
    width: ?u32 = null,
    height: ?u32 = null,
    color: []const u8 = "white",
};

/// Which frame --layout coordinates are in (llm-contract.md §1): `current` (default) is the already
/// cropped/rotated image; `source` re-maps the layout's points through the resolved crop/rotation.
pub const LayoutFrame = enum { current, source };

pub const Options = struct {
    help: bool = false,
    console: bool = false,
    console_full_screen: bool = false, // --console-full-screen: pinned logo header + scrollback + mouse
    input: ?[]const u8 = null,
    frame: u32 = 0,
    blank: ?Blank = null,
    crop: ?[]const u8 = null,
    album: bool = false,
    rotate: i32 = 0,
    layout: ?[]const u8 = null,
    layout_frame: LayoutFrame = .current,
    filter: ?[]const u8 = null,
    output: ?[]const u8 = null,
    confine_output: bool = false, // --confine-output: output + scrape dir stay inside the cwd

    /// `--script <file>` runs a .stc; `--script-plan` and `--script-check` report it on stdout, and
    /// `--script-emit <out>` re-writes it for the surface <out>'s extension names. "-" reads stdin.
    script: ?[]const u8 = null,
    script_plan: ?[]const u8 = null,
    script_check: ?[]const u8 = null,
    script_emit: ?[]const u8 = null,
    // Server options: `--server <url>` + -i names a project to fetch and edit (`--remote-update` writes
    // back); `--remote` + `--remote-name` upload the result as a NEW project; `--token` authenticates.
    server: ?[]const u8 = null,
    remote: ?[]const u8 = null,
    remote_name: ?[]const u8 = null,
    remote_update: bool = false,
    token: ?[]const u8 = null,
    // Source-site scraping. --source-site <url> activates scrape mode (mutually exclusive
    // with -i/--blank/--server): fetch the page, filter its media, download into <output> (a dir).
    source_site: ?[]const u8 = null,
    source_count: ?u32 = null, // items per page/group; absent = default 5, 0 = all (applied in scrape.effectiveCount)
    group: u32 = 0, // 0-based page index; window = filtered[G*N : G*N+N]
    source_filter: ?[]const u8 = null, // category tokens, '|'-separated (absent = all)
    source_format: ?[]const u8 = null, // format tokens, '|'-separated (absent = all)
    source_name: ?[]const u8 = null, // regex (POSIX ERE, case-insensitive) on the media URL; absent = all
    source_min_width: u32 = 0, // inclusive; 0 = unset
    source_max_width: u32 = 0,
    source_min_height: u32 = 0,
    source_max_height: u32 = 0,
};

/// Which of the four `.stc` entry points `--script*` selected.
pub const ScriptMode = enum { run, plan, check, emit };

/// Which command mode a parsed Options selected. The modes are exclusive and read DIFFERENT blocks of
/// the struct, so deciding once here keeps main.zig from re-deriving the exclusions.
pub const Mode = union(enum) {
    /// `--help`, or no arguments at all: banner + usage.
    usage,
    /// `--console` / `--repl`: the interactive REPL, with or without the full-screen header.
    console: struct { full_screen: bool },
    /// `--source-site <url>`: scrape a page into `output`; the editing flags do not apply.
    scrape,
    /// `--script` / `--script-plan` / `--script-check` / `--script-emit`: a .stc drives the edits.
    script: struct { kind: ScriptMode, path: []const u8 },
    /// A `.stencil` project on either side, outside server mode: the console Session renders
    /// the layout the way the GUI editors do.
    project,
    /// Everything else: source -> crop -> rotate -> filter -> layout -> encode.
    pipeline,
};

/// The mode `opts` selected, in the order the flags override each other. `argc` is the count
/// of arguments the user actually passed, so a bare `stencil` shows usage.
pub fn modeOf(opts: Options, argc: usize, isProjectPath: *const fn ([]const u8) bool) Mode {
    if (opts.help or argc == 0) return .usage;
    if (opts.console) return .{ .console = .{ .full_screen = opts.console_full_screen } };
    if (opts.script) |p| return .{ .script = .{ .kind = if (opts.script_emit != null) .emit else .run, .path = p } };
    if (opts.script_plan) |p| return .{ .script = .{ .kind = .plan, .path = p } };
    if (opts.script_check) |p| return .{ .script = .{ .kind = .check, .path = p } };
    if (opts.source_site != null) return .scrape;
    const proj = (opts.input != null and isProjectPath(opts.input.?)) or
        (opts.output != null and isProjectPath(opts.output.?));
    if (opts.server == null and proj) return .project;
    return .pipeline;
}

test "modeOf: usage wins, then console, then scrape, then a .stencil path" {
    const isStencil = struct {
        fn f(p: []const u8) bool {
            return std.mem.endsWith(u8, p, ".stencil");
        }
    }.f;
    try std.testing.expectEqual(Mode.usage, modeOf(.{}, 0, isStencil));
    try std.testing.expectEqual(Mode.usage, modeOf(.{ .help = true }, 3, isStencil));
    try std.testing.expect(modeOf(.{ .console = true, .console_full_screen = true }, 1, isStencil).console.full_screen);
    try std.testing.expectEqual(Mode.scrape, modeOf(.{ .source_site = "http://x/", .console = false }, 2, isStencil));
    try std.testing.expectEqual(Mode.project, modeOf(.{ .input = "a.stencil" }, 2, isStencil));
    try std.testing.expectEqual(Mode.project, modeOf(.{ .output = "a.stencil" }, 2, isStencil));
    // Server mode keeps the raster pipeline even when a name looks like a project path.
    try std.testing.expectEqual(Mode.pipeline, modeOf(.{ .input = "a.stencil", .server = "http://s/" }, 4, isStencil));
    try std.testing.expectEqual(Mode.pipeline, modeOf(.{ .input = "a.png" }, 2, isStencil));
    // --script-emit is the same script mode, told apart by what it does with the file.
    const run = modeOf(.{ .script = "s.stc" }, 2, isStencil).script;
    try std.testing.expectEqual(ScriptMode.run, run.kind);
    const emit = modeOf(.{ .script = "s.stc", .script_emit = "s.pystc" }, 4, isStencil).script;
    try std.testing.expectEqual(ScriptMode.emit, emit.kind);
    try std.testing.expectEqualStrings("s.stc", emit.path);
}

pub const Error = error{
    MissingValue,
    BadNumber,
    BadValue,
    UnknownFlag,
    DuplicateSource,
};

//! The report and project-mode flags: `--probe` measures the -i input, `--list-projects` and
//! `--project-info` read a --server, and `--project-update` / `--project-files` / `--project-file`
//! write, list or download one of its projects. One mode at a time; the `--set-*` fields ride
//! only with `--project-update`, which needs at least one.
const std = @import("std");
const testing = std.testing;
const core = @import("../core.zig");
const logo = @import("../app/logo.zig");
const options = @import("options.zig");
const state = @import("state.zig");
const parser = @import("parse.zig");

const Error = options.Error;
const Options = options.Options;
const ProjectSet = options.ProjectSet;
const ParseState = state.ParseState;
const eq = state.eq;

/// The kinds the server's files route serves (server/internal/protocol/filekinds.go).
pub const file_kinds = [_][]const u8{
    "original", "result",   "video",    "chat",     "variant1", "variant2",
    "variant3", "variant4", "variant5", "variant6", "variant7", "variant8",
};

/// Consume `arg` when it is a report or project-mode flag; false leaves it to the caller.
pub fn flag(opts: *Options, arg: []const u8, st: *ParseState) Error!bool {
    const before = asked(opts.*);
    if (eq(arg, "--probe")) {
        opts.probe = true;
    } else if (eq(arg, "--list-projects")) {
        opts.list_projects = true;
    } else if (eq(arg, "--project-info")) {
        opts.project_info = try state.value(st, "--project-info");
    } else if (eq(arg, "--project-update")) {
        opts.project_update = try projectId(st, "--project-update");
    } else if (eq(arg, "--project-files")) {
        opts.project_files = try projectId(st, "--project-files");
    } else if (eq(arg, "--project-file")) {
        const id = try projectId(st, "--project-file");
        const kind = try state.value(st, "--project-file");
        for (file_kinds) |k| {
            if (eq(k, kind)) break;
        } else {
            logo.err("--project-file expects a kind: original, result, video, chat or variant1..variant8, got '{s}'\n", .{kind});
            return Error.BadValue;
        }
        opts.project_file = .{ .id = id, .kind = kind };
    } else return setFlag(&opts.set, arg, st);
    if (before != 0 and asked(opts.*) > before) return Error.DuplicateSource;
    return true;
}

fn setFlag(set: *ProjectSet, arg: []const u8, st: *ParseState) Error!bool {
    if (eq(arg, "--set-name")) {
        const name = try state.value(st, "--set-name");
        if (std.mem.trim(u8, name, " \t").len == 0) {
            logo.err("--set-name needs a name; a project's name cannot be cleared\n", .{});
            return Error.BadValue;
        }
        set.name = name;
    } else if (eq(arg, "--set-description")) {
        set.description = try state.value(st, "--set-description");
    } else if (eq(arg, "--set-keywords")) {
        set.keywords = try state.value(st, "--set-keywords");
    } else if (eq(arg, "--set-color")) {
        set.color = try color(st, "--set-color");
    } else if (eq(arg, "--set-blank-color")) {
        set.blank_color = try color(st, "--set-blank-color");
    } else if (eq(arg, "--set-expires")) {
        set.expires = try count(st, "--set-expires");
    } else if (eq(arg, "--if-version")) {
        set.if_version = try count(st, "--if-version");
    } else return false;
    return true;
}

/// A project id as the server allocates one (`p_<base36>_<base36>`); it becomes a URL path segment.
fn projectId(st: *ParseState, name: []const u8) Error![]const u8 {
    const id = try state.value(st, name);
    const safe = id.len > 0 and id.len <= 128 and for (id) |b| {
        if (!std.ascii.isAlphanumeric(b) and b != '_' and b != '-') break false;
    } else true;
    if (!safe) {
        logo.err("{s} expects a project id (p_…, as --list-projects prints it), got '{s}'\n", .{ name, id });
        return Error.BadValue;
    }
    return id;
}

/// A colour core parses (a `#hex` or a colour name), or "" to clear it.
fn color(st: *ParseState, name: []const u8) Error![:0]const u8 {
    const v = try state.value(st, name);
    if (v.len != 0 and core.parseColor(v) == null) {
        logo.err("{s} expects a #rrggbb colour, a colour name or '', got '{s}'\n", .{ name, v });
        return Error.BadValue;
    }
    return v;
}

fn count(st: *ParseState, name: []const u8) Error!i64 {
    const v = try state.value(st, name);
    const n = std.fmt.parseInt(i64, v, 10) catch return Error.BadNumber;
    if (n < 0) return Error.BadNumber;
    return n;
}

fn asked(o: Options) u8 {
    const project = @as(u8, @intFromBool(o.project_update != null)) + @intFromBool(o.project_files != null) +
        @intFromBool(o.project_file != null);
    return project + @as(u8, @intFromBool(o.probe)) + @intFromBool(o.list_projects) + @intFromBool(o.project_info != null);
}

/// The rules only the whole argv can judge: `--set-*` rides with `--project-update`, which needs one.
pub fn finish(opts: Options) Error!void {
    if (opts.project_update == null) {
        if (!opts.set.any() and opts.set.if_version == null) return;
        logo.err("the --set-* fields and --if-version ride with --project-update <id>\n", .{});
        return Error.BadValue;
    }
    if (opts.set.any()) return;
    logo.err("--project-update needs a field to change: --set-name, --set-description, --set-keywords, --set-color, --set-blank-color or --set-expires\n", .{});
    return Error.BadValue;
}

test "parse: the reporting modes, one at a time" {
    const probe = [_][:0]const u8{ "--probe", "-i", "a.png" };
    const p = try parser.parse(&probe);
    try testing.expect(p.probe);
    try testing.expectEqualStrings("a.png", p.input.?);

    const list = [_][:0]const u8{ "--server", "https://h", "--list-projects" };
    try testing.expect((try parser.parse(&list)).list_projects);
    const info = [_][:0]const u8{ "--project-info", "Plans", "--server", "https://h" };
    try testing.expectEqualStrings("Plans", (try parser.parse(&info)).project_info.?);

    const twice = [_][:0]const u8{ "--probe", "--probe", "-i", "a.png" };
    try testing.expect((try parser.parse(&twice)).probe);
    const both = [_][:0]const u8{ "--list-projects", "--project-info", "p_1" };
    try testing.expectError(Error.DuplicateSource, parser.parse(&both));
    const probe_list = [_][:0]const u8{ "--probe", "-i", "a.png", "--list-projects" };
    try testing.expectError(Error.DuplicateSource, parser.parse(&probe_list));
    const bare = [_][:0]const u8{"--project-info"};
    try testing.expectError(Error.MissingValue, parser.parse(&bare));
}

test "parse: --project-update carries its --set-* fields, '' kept as a clear" {
    const argv = [_][:0]const u8{
        "--server",       "https://h", "--project-update", "p_1_a", "--set-name",        "Plans v2",
        "--set-keywords", "a, b",      "--set-color",      "red",   "--set-description", "",
        "--set-expires",  "0",         "--if-version",     "7",
    };
    const o = try parser.parse(&argv);
    try testing.expectEqualStrings("p_1_a", o.project_update.?);
    try testing.expectEqualStrings("Plans v2", o.set.name.?);
    try testing.expectEqualStrings("a, b", o.set.keywords.?);
    try testing.expectEqualStrings("red", o.set.color.?);
    try testing.expectEqualStrings("", o.set.description.?);
    try testing.expectEqual(@as(?i64, 0), o.set.expires);
    try testing.expectEqual(@as(?i64, 7), o.set.if_version);
    try testing.expect(o.set.blank_color == null);
    try testing.expectEqual(options.Mode{ .inspect = .project_update }, options.modeOf(o, argv.len, isStencil));
}

test "parse: the project modes refuse what they cannot mean" {
    const bad = [_][]const [:0]const u8{
        &.{ "--project-update", "p_1_a" }, // nothing to change
        &.{ "--set-name", "x" }, // a field with no project
        &.{ "--if-version", "3", "--project-files", "p_1_a" },
        &.{ "--project-update", "p_1_a", "--set-name", " " },
        &.{ "--project-update", "p_1_a", "--set-color", "not-a-colour" },
        &.{ "--project-update", "../admin", "--set-name", "x" },
        &.{ "--project-file", "p_1_a", "thumbnail", "out.png" },
        &.{ "--project-files", "p/1" },
    };
    for (bad) |argv| try testing.expectError(Error.BadValue, parser.parse(argv));
    const negative = [_][:0]const u8{ "--project-update", "p_1_a", "--set-expires", "-5" };
    try testing.expectError(Error.BadNumber, parser.parse(&negative));
    const two = [_][:0]const u8{ "--project-files", "p_1_a", "--project-file", "p_1_a", "result", "o" };
    try testing.expectError(Error.DuplicateSource, parser.parse(&two));
    const with_probe = [_][:0]const u8{ "--probe", "-i", "a.png", "--project-files", "p_1_a" };
    try testing.expectError(Error.DuplicateSource, parser.parse(&with_probe));
}

test "parse: --project-file takes an id and a kind, its output positional" {
    const argv = [_][:0]const u8{ "--server", "http://h:8090", "--project-file", "p_1_a", "variant3", "v3.png" };
    const o = try parser.parse(&argv);
    try testing.expectEqualStrings("p_1_a", o.project_file.?.id);
    try testing.expectEqualStrings("variant3", o.project_file.?.kind);
    try testing.expectEqualStrings("v3.png", o.output.?);
    try testing.expectEqual(options.Mode{ .inspect = .project_file }, options.modeOf(o, argv.len, isStencil));
    const blank = [_][:0]const u8{ "--project-update", "p_1_a", "--set-blank-color", "" };
    try testing.expectEqualStrings("", (try parser.parse(&blank)).set.blank_color.?);
}

fn isStencil(path: []const u8) bool {
    return std.ascii.endsWithIgnoreCase(path, ".stencil");
}

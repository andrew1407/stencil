//! The op-plan flags: `--plan-check <file|->` walks a model reply through core's validator and
//! prints its result; `--plan-surface` and `--plan-capabilities` choose the schema it walks under,
//! and also ride with `--script-plan`, whose chunks then carry that surface's verdict. `--prompt`
//! asks the configured model for a plan and runs it on a one-shot input. `--merge-lines` is the
//! other report mode an adapter drives core through: the co-edit line union.
const std = @import("std");
const logo = @import("../app/logo.zig");
const testing = std.testing;
const opplan = @import("../core/opplan.zig");
const options = @import("options.zig");
const state = @import("state.zig");
const parser = @import("parse.zig");

const Error = options.Error;
const Options = options.Options;
const ParseState = state.ParseState;
const eq = state.eq;

/// Consume `arg` when it is an op-plan flag; false leaves it to the caller.
pub fn flag(opts: *Options, arg: []const u8, st: *ParseState) Error!bool {
    if (eq(arg, "--plan-check")) {
        opts.plan_check = try state.value(st, "--plan-check");
    } else if (eq(arg, "--plan-surface")) {
        const v = try state.value(st, "--plan-surface");
        if (!opplan.isSurface(v)) {
            logo.err("--plan-surface expects cli, mcp, bot, pystencil, desktop or browser, got '{s}'\n", .{v});
            return Error.BadValue;
        }
        opts.plan_surface = v;
    } else if (eq(arg, "--plan-capabilities")) {
        opts.plan_capabilities = try state.value(st, "--plan-capabilities");
    } else if (eq(arg, "--merge-lines")) {
        opts.merge_lines = try state.value(st, "--merge-lines");
    } else if (eq(arg, "--prompt")) {
        opts.prompt = try state.value(st, "--prompt");
    } else return false;
    return true;
}

/// The rules only the whole argv can judge: one reporting mode at a time, and a schema choice
/// only where a plan is walked.
pub fn finish(opts: Options) Error!void {
    const reporting = opts.script != null or opts.script_plan != null or opts.script_check != null or
        opts.probe or opts.list_projects or opts.project_info != null or opts.source_site != null or
        opts.project_update != null or opts.project_files != null or opts.project_file != null;
    if (opts.plan_check != null and reporting) return Error.DuplicateSource;
    if (opts.merge_lines != null and (reporting or opts.plan_check != null or opts.console)) return Error.DuplicateSource;
    if ((opts.plan_surface != null or opts.plan_capabilities != null) and opts.plan_check == null and opts.script_plan == null) {
        logo.err("--plan-surface and --plan-capabilities ride with --plan-check or --script-plan\n", .{});
        return Error.BadValue;
    }
    if (opts.prompt == null) return;
    if (reporting or opts.plan_check != null or opts.merge_lines != null or opts.console) return Error.DuplicateSource;
    if (opts.server != null or opts.remote != null or opts.remote_update) {
        logo.err("--prompt edits a local input; it does not ride with --server or --remote\n", .{});
        return Error.BadValue;
    }
    // Checked before a paid call is made, not after.
    if (opts.output == null) {
        logo.err("--prompt writes its result to an output path — name one\n", .{});
        return Error.BadValue;
    }
}

test "parse: --plan-check with its surface and capabilities" {
    const argv = [_][:0]const u8{ "--plan-check", "-", "--plan-surface", "mcp", "--plan-capabilities", "loadAttachment,saveProject" };
    const o = try parser.parse(&argv);
    try testing.expectEqualStrings("-", o.plan_check.?);
    try testing.expectEqualStrings("mcp", o.plan_surface.?);
    try testing.expectEqualStrings("loadAttachment,saveProject", o.plan_capabilities.?);
    const plain = [_][:0]const u8{ "--plan-check", "reply.txt" };
    try testing.expect((try parser.parse(&plain)).plan_surface == null); // cli by default
}

test "parse: --plan-surface rides with --script-plan too" {
    const argv = [_][:0]const u8{ "--script-plan", "s.stc", "--plan-surface", "bot" };
    const o = try parser.parse(&argv);
    try testing.expectEqualStrings("bot", o.plan_surface.?);
}

test "parse: --prompt rides a one-shot input with an output, and nothing else" {
    const ok = [_][:0]const u8{ "-i", "a.png", "--prompt", "crop 10% off the left", "out.png" };
    try testing.expectEqualStrings("crop 10% off the left", (try parser.parse(&ok)).prompt.?);
    const no_out = [_][:0]const u8{ "-i", "a.png", "--prompt", "x" };
    try testing.expectError(Error.BadValue, parser.parse(&no_out));
    const remote = [_][:0]const u8{ "-i", "a.png", "--prompt", "x", "--remote", "http://h:1", "out.png" };
    try testing.expectError(Error.BadValue, parser.parse(&remote));
    const script = [_][:0]const u8{ "--script", "s.stc", "--prompt", "x", "out.png" };
    try testing.expectError(Error.DuplicateSource, parser.parse(&script));
}

test "parse: the op-plan flags refuse what they cannot mean" {
    const unknown = [_][:0]const u8{ "--plan-check", "-", "--plan-surface", "extension" };
    try testing.expectError(Error.BadValue, parser.parse(&unknown));
    const alone = [_][:0]const u8{ "--plan-surface", "bot", "-i", "a.png", "out.png" };
    try testing.expectError(Error.BadValue, parser.parse(&alone));
    const with_probe = [_][:0]const u8{ "--plan-check", "-", "--probe", "-i", "a.png" };
    try testing.expectError(Error.DuplicateSource, parser.parse(&with_probe));
    const with_script = [_][:0]const u8{ "--plan-check", "-", "--script", "s.stc" };
    try testing.expectError(Error.DuplicateSource, parser.parse(&with_script));
    const bare = [_][:0]const u8{"--plan-check"};
    try testing.expectError(Error.MissingValue, parser.parse(&bare));
}

test "parse: --merge-lines is a report mode of its own" {
    const ok = [_][:0]const u8{ "--merge-lines", "-" };
    try testing.expectEqualStrings("-", (try parser.parse(&ok)).merge_lines.?);
    const with_plan = [_][:0]const u8{ "--merge-lines", "-", "--plan-check", "r.txt" };
    try testing.expectError(Error.DuplicateSource, parser.parse(&with_plan));
    const with_probe = [_][:0]const u8{ "--merge-lines", "-", "--probe", "-i", "a.png" };
    try testing.expectError(Error.DuplicateSource, parser.parse(&with_probe));
    const with_prompt = [_][:0]const u8{ "--merge-lines", "-", "-i", "a.png", "--prompt", "x", "out.png" };
    try testing.expectError(Error.DuplicateSource, parser.parse(&with_prompt));
    const bare = [_][:0]const u8{"--merge-lines"};
    try testing.expectError(Error.MissingValue, parser.parse(&bare));
}

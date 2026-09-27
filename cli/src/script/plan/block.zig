//! One `--script-plan` block: its inputs, the size probe, the op-plan chunks resolved against
//! the first input, the lowered op stream with every length token as written, and the same
//! chunks resolved per input against that input's own size. The envelope around it is plan.zig.
const std = @import("std");

const args = @import("../../args.zig");
const image = @import("../../media/image.zig");
const scriptCore = @import("../core.zig");

const probe = @import("probe.zig");
const sequence = @import("sequence.zig");
const save_mod = @import("../save.zig");
const sources = @import("../sources.zig");
const opplan = @import("../../core/opplan.zig");

fn fmtOf(path: []const u8) image.Format {
    return image.formatOfPath(path) orelse .png;
}

/// The concrete files a `@save` writes, one entry per input × save op — the same naming
/// `--script` would use, so an adapter can report a destination without running anything.
fn writeSaves(
    a: std.mem.Allocator,
    js: *std.json.Stringify,
    script: scriptCore.Script,
    block: scriptCore.Block,
    inputs: []const []const u8,
) !void {
    try js.beginArray();
    for (inputs) |input| {
        const fmt = fmtOf(input);
        var frame = block.frame;
        var i: u32 = block.op_start;
        while (i < block.op_start + block.op_count) : (i += 1) {
            const op = script.op(i) orelse continue;
            if (op.kind == .frame) {
                frame = @intFromFloat(@max(0, script.opNum(i, 0) orelse 0));
                continue;
            }
            if (op.kind != .save) continue;
            try js.beginObject();
            try js.objectField("input");
            try js.write(input);
            try js.objectField("path");
            try js.write(try save_mod.resolveTarget(a, script.opStr(i, 0), input, save_mod.frameOf(frame), fmt));
            try js.endObject();
        }
    }
    try js.endArray();
}

fn writeDims(js: *std.json.Stringify, dims: ?sequence.Dims) !void {
    const d = dims orelse return js.write(null);
    try js.beginObject();
    try js.objectField("width");
    try js.write(@as(i64, @intFromFloat(d.w)));
    try js.objectField("height");
    try js.write(@as(i64, @intFromFloat(d.h)));
    try js.endObject();
}

/// One entry per input, in `inputs` order: its own probed size, the plans resolved against it
/// (opening it), and its saves — so a block over files of different sizes gets each one's pixels.
fn writePerInput(
    a: std.mem.Allocator,
    env: *sequence.Env,
    js: *std.json.Stringify,
    script: scriptCore.Script,
    block: scriptCore.Block,
    inputs: []const []const u8,
    first_dims: ?sequence.Dims,
    checker: ?opplan.Schema,
) !void {
    try js.beginArray();
    for (inputs, 0..) |input, k| {
        const dims = if (k == 0) first_dims else try probe.size(a, env, script, block, input);
        try js.beginObject();
        try js.objectField("input");
        try js.write(input);
        try js.objectField("dims");
        try writeDims(js, dims);
        try js.objectField("plans");
        try writePlans(a, js, try sequence.build(a, env, script, block, input, dims), checker);
        try js.objectField("saves");
        try writeSaves(a, js, script, block, &.{input});
        try js.endObject();
    }
    try js.endArray();
}

/// The block's lowered op stream (stc-contract §13), length tokens unresolved as --script-emit
/// writes them: what `plans` resolved against the first input, for any other input to resolve.
fn writeOps(js: *std.json.Stringify, script: scriptCore.Script, block: scriptCore.Block) !void {
    try js.beginArray();
    var i: u32 = block.op_start;
    while (i < block.op_start + block.op_count) : (i += 1) {
        const op = script.op(i) orelse continue;
        try js.beginObject();
        try js.objectField("kind");
        try js.write(@tagName(op.kind));
        try js.objectField("line");
        try js.write(op.line);
        try js.objectField("edit");
        try js.write(op.edit_index);
        try js.objectField("strs");
        try js.beginArray();
        for (0..op.str_count) |k| try js.write(script.opStr(i, @intCast(k)));
        try js.endArray();
        try js.objectField("toks");
        try js.beginArray();
        for (0..script.opTokCount(i)) |k| try js.write(script.opTok(i, @intCast(k)));
        try js.endArray();
        try js.objectField("nums");
        try js.beginArray();
        for (0..op.num_count) |k| try js.write(script.opNum(i, @intCast(k)) orelse 0);
        try js.endArray();
        try js.endObject();
    }
    try js.endArray();
}

/// The block's plans, in order; an empty block yields an empty array. With a `checker`
/// (`--plan-surface`), each plan also carries that surface's core verdict as `check`.
fn writePlans(a: std.mem.Allocator, js: *std.json.Stringify, plans: []const []const std.json.Value, checker: ?opplan.Schema) !void {
    try js.beginArray();
    for (plans) |chunk| {
        try js.beginObject();
        try js.objectField("reply");
        try js.write("");
        try js.objectField("actions");
        try js.write(chunk);
        if (checker) |schema| {
            const plan = try std.json.Stringify.valueAlloc(a, .{ .reply = "", .actions = chunk }, .{});
            const walked = try schema.walk(a, plan);
            try js.objectField("check");
            try js.beginWriteRaw();
            try js.writer.writeAll(walked.json);
            js.endWriteRaw();
        }
        try js.endObject();
    }
    try js.endArray();
}

/// error.PlanRefused leaves the reason in `env.refusal`.
pub fn writeBlock(
    a: std.mem.Allocator,
    env: *sequence.Env,
    js: *std.json.Stringify,
    script: scriptCore.Script,
    opts: args.Options,
    index: u32,
    checker: ?opplan.Schema,
) !void {
    const block = script.block(index) orelse return;
    var inputs: []const []const u8 = &.{};
    if (block.kind == .project) {
        // No @source: the block edits whatever -i named, exactly as the runner does.
        if (opts.input) |in| inputs = try a.dupe([]const u8, &.{in});
    } else {
        // A source that resolved to nothing still plans, with no inputs: §4.3 promises exactly
        // one envelope on stdout. `expand` has already said on stderr what went wrong.
        inputs = sources.expand(a, env.io, block.source, block.kind) catch &.{};
    }
    const first = if (inputs.len > 0) inputs[0] else "";
    const dims = try probe.size(a, env, script, block, first);

    try js.beginObject();
    try js.objectField("index");
    try js.write(index);
    try js.objectField("source");
    try js.write(block.source);
    try js.objectField("sourceKind");
    try js.write(@tagName(block.kind));
    try js.objectField("inputs");
    try js.write(inputs);
    try js.objectField("frame");
    try js.write(block.frame);
    try js.objectField("dims");
    try writeDims(js, dims);
    try js.objectField("plans");
    try writePlans(a, js, try sequence.build(a, env, script, block, first, dims), checker);
    try js.objectField("saves");
    try writeSaves(a, js, script, block, inputs);
    try js.objectField("ops");
    try writeOps(js, script, block);
    try js.objectField("perInput");
    try writePerInput(a, env, js, script, block, inputs, dims, checker);
    try js.endObject();
}

test {
    _ = probe;
    _ = sequence;
}

const testing = std.testing;

test "an extension picks the save format, unknown falls back to png" {
    try testing.expectEqual(image.Format.jpeg, fmtOf("https://e.example/a.JPG?x=1"));
    try testing.expectEqual(image.Format.png, fmtOf("clip.mp4"));
}

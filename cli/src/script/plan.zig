//! `--script-plan <file>`: the script lowered to op-plan JSON on STDOUT, for the adapters
//! that drive an editor rather than the pixels (mcp, bot). Nothing is fetched and nothing
//! is written — a header-only size probe of each block's first local input is the only I/O.
//! The writer comes in from main.zig: opening a terminal is the console layer's privilege.
const std = @import("std");

const args = @import("../args.zig");
const image = @import("../media/image.zig");
const net = @import("../net.zig");
const scriptCore = @import("core.zig");
const video = @import("../media/video.zig");

const load = @import("load.zig");
const planActions = @import("planActions.zig");
const save_mod = @import("save.zig");
const sources = @import("sources.zig");

/// Envelope version. Bumped only when a consumer has to change; see cli/CONTRACT.md §5.
pub const VERSION: i64 = 1;

/// A copy of the op-plan envelope's `limits.MAX_ACTIONS` (browser/js/config/llm/opRegistry.json),
/// which this layer sits below; tests/script_test.zig pins the two together.
pub const MAX_ACTIONS: usize = 16;

/// How much of an input's HEAD is read to find its header. Well past any SOF marker, and a
/// PREFIX rather than a capped whole-file read: a photo bigger than this still probes.
const PROBE_BYTES: usize = 4 << 20;

fn fmtOf(path: []const u8) image.Format {
    return image.formatOfPath(path) orelse .png;
}

/// The size lengths resolve against, or null when the input is not a local still this
/// build can read a header from (a URL, a video, a missing file).
fn probeDims(gpa: std.mem.Allocator, io: std.Io, input: []const u8) ?planActions.Dims {
    if (input.len == 0 or net.isUrl(input) or video.looksLikeVideo(input)) return null;
    var file = std.Io.Dir.cwd().openFile(io, input, .{}) catch return null;
    defer file.close(io);
    const head = gpa.alloc(u8, PROBE_BYTES) catch return null;
    defer gpa.free(head);
    var buf: [4096]u8 = undefined;
    var reader = file.readerStreaming(io, &buf);
    const n = reader.interface.readSliceShort(head) catch return null;
    const d = image.dims(head[0..n]) orelse return null;
    return .{ .w = @floatFromInt(d.width), .h = @floatFromInt(d.height) };
}

fn writeDiagnostics(js: *std.json.Stringify, script: scriptCore.Script) !void {
    try js.beginArray();
    var i: u32 = 0;
    while (i < script.diagnosticCount()) : (i += 1) {
        const d = script.diagnostic(i) orelse continue;
        try js.beginObject();
        try js.objectField("severity");
        try js.write(if (d.severity == .err) "error" else "warning");
        try js.objectField("code");
        try js.write(d.code);
        try js.objectField("line");
        try js.write(d.line);
        try js.objectField("col");
        try js.write(d.col);
        try js.objectField("len");
        try js.write(d.len);
        try js.objectField("message");
        try js.write(d.message);
        try js.endObject();
    }
    try js.endArray();
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

/// One plan per `MAX_ACTIONS` actions, in order; an empty block yields an empty array.
fn writePlans(js: *std.json.Stringify, actions: []const std.json.Value) !void {
    try js.beginArray();
    var start: usize = 0;
    while (start < actions.len) : (start += MAX_ACTIONS) {
        try js.beginObject();
        try js.objectField("reply");
        try js.write("");
        try js.objectField("actions");
        try js.beginArray();
        for (actions[start..@min(start + MAX_ACTIONS, actions.len)]) |v| try js.write(v);
        try js.endArray();
        try js.endObject();
    }
    try js.endArray();
}

fn writeBlock(
    a: std.mem.Allocator,
    io: std.Io,
    js: *std.json.Stringify,
    script: scriptCore.Script,
    opts: args.Options,
    index: u32,
) !void {
    const block = script.block(index) orelse return;
    var inputs: []const []const u8 = &.{};
    if (block.kind == .project) {
        // No @source: the block edits whatever -i named, exactly as the runner does.
        if (opts.input) |in| inputs = try a.dupe([]const u8, &.{in});
    } else {
        // A source that resolved to nothing still plans, with no inputs: §4.3 promises exactly
        // one envelope on stdout. `expand` has already said on stderr what went wrong.
        inputs = sources.expand(a, io, block.source, block.kind) catch &.{};
    }
    const first = if (inputs.len > 0) inputs[0] else "";
    const dims = probeDims(a, io, first);

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
    if (dims) |d| {
        try js.beginObject();
        try js.objectField("width");
        try js.write(@as(i64, @intFromFloat(d.w)));
        try js.objectField("height");
        try js.write(@as(i64, @intFromFloat(d.h)));
        try js.endObject();
    } else try js.write(null);
    try js.objectField("plans");
    try writePlans(js, try planActions.build(a, script, block, first, dims));
    try js.objectField("saves");
    try writeSaves(a, js, script, block, inputs);
    try js.endObject();
}

/// The whole envelope. A script with an error still reports its diagnostics, but lowers to
/// no blocks at all — nothing in it is safe to act on.
pub fn writeEnvelope(
    gpa: std.mem.Allocator,
    io: std.Io,
    out: *std.Io.Writer,
    script: scriptCore.Script,
    opts: args.Options,
    label: []const u8,
) !void {
    var arena: std.heap.ArenaAllocator = .init(gpa);
    defer arena.deinit();
    const a = arena.allocator();

    var js: std.json.Stringify = .{ .writer = out };
    try js.beginObject();
    try js.objectField("version");
    try js.write(VERSION);
    try js.objectField("script");
    try js.write(label);
    try js.objectField("diagnostics");
    try writeDiagnostics(&js, script);
    try js.objectField("blocks");
    try js.beginArray();
    if (!script.hasErrors()) {
        var b: u32 = 0;
        while (b < script.blockCount()) : (b += 1) try writeBlock(a, io, &js, script, opts, b);
    }
    try js.endArray();
    try js.endObject();
    try out.writeByte('\n');
}

pub fn run(gpa: std.mem.Allocator, io: std.Io, out: *std.Io.Writer, opts: args.Options, path: []const u8) !void {
    const source = try load.readScript(gpa, io, path);
    defer gpa.free(source);

    var script = scriptCore.Script.parse(source) catch return load.Error.ScriptUnreadable;
    defer script.deinit();

    try writeEnvelope(gpa, io, out, script, opts, load.labelFor(path));
    try out.flush();
    if (script.hasErrors()) return load.Error.ScriptHasErrors;
}

const testing = std.testing;

test "an extension picks the save format, unknown falls back to png" {
    try testing.expectEqual(image.Format.jpeg, fmtOf("https://e.example/a.JPG?x=1"));
    try testing.expectEqual(image.Format.png, fmtOf("clip.mp4"));
}

test "the envelope carries the version, the label and one block per @source" {
    const gpa = testing.allocator;
    var threaded = std.Io.Threaded.init(gpa, .{});
    defer threaded.deinit();

    var s = try scriptCore.Script.parse("@source a.png:\n  @filter bw\n  @save\n");
    defer s.deinit();

    var out: std.Io.Writer.Allocating = .init(gpa);
    defer out.deinit();
    try writeEnvelope(gpa, threaded.io(), &out.writer, s, .{}, "a.stc");

    var parsed = try std.json.parseFromSlice(std.json.Value, gpa, out.written(), .{});
    defer parsed.deinit();
    const root = parsed.value.object;
    try testing.expectEqual(@as(i64, VERSION), root.get("version").?.integer);
    try testing.expectEqualStrings("a.stc", root.get("script").?.string);
    try testing.expectEqual(@as(usize, 0), root.get("diagnostics").?.array.items.len);

    const block = root.get("blocks").?.array.items[0].object;
    try testing.expectEqualStrings("file", block.get("sourceKind").?.string);
    try testing.expectEqualStrings("a.png", block.get("inputs").?.array.items[0].string);
    try testing.expectEqualStrings("a-stencil.png", block.get("saves").?.array.items[0].object.get("path").?.string);
}

test "a script with an error reports its diagnostics and lowers to no blocks" {
    const gpa = testing.allocator;
    var threaded = std.Io.Threaded.init(gpa, .{});
    defer threaded.deinit();

    var s = try scriptCore.Script.parse("@source a.png:\n  @crp 10%\n");
    defer s.deinit();

    var out: std.Io.Writer.Allocating = .init(gpa);
    defer out.deinit();
    try writeEnvelope(gpa, threaded.io(), &out.writer, s, .{}, "a.stc");

    var parsed = try std.json.parseFromSlice(std.json.Value, gpa, out.written(), .{});
    defer parsed.deinit();
    try testing.expectEqual(@as(usize, 0), parsed.value.object.get("blocks").?.array.items.len);
    const diag = parsed.value.object.get("diagnostics").?.array.items[0].object;
    try testing.expectEqualStrings("error", diag.get("severity").?.string);
    try testing.expectEqualStrings("E_UNKNOWN_DIRECTIVE", diag.get("code").?.string);
}

//! `--script <file>`: walk the lowered op stream, one block at a time, over every input
//! that block names. The core decided WHAT to do; this decides what that means for a file.
const std = @import("std");

const args = @import("../args.zig");
const confine = @import("../confine.zig");
const image = @import("../image.zig");
const page_mod = @import("../page.zig");
const pipeline = @import("../pipeline.zig");
const report = @import("../report.zig");
const scriptCore = @import("../scriptCore.zig");
const video = @import("../video.zig");

const apply = @import("apply.zig");
const load = @import("load.zig");
const save_mod = @import("save.zig");
const sources = @import("sources.zig");

pub const Error = error{ NoScriptInput, FrameNeedsVideo };

/// The editing state one input carries through a block.
const Canvas = struct {
    img: image.Rgba8,
    fmt: image.Format,
    source: []const u8,
    frame: ?u32 = null,
    lines: std.ArrayList(u8), // the layout JSON accumulated by @line / @rect / @layout

    fn deinit(self: *Canvas, gpa: std.mem.Allocator) void {
        self.img.deinit(gpa);
        self.lines.deinit(gpa);
    }
};

fn openInput(gpa: std.mem.Allocator, io: std.Io, path: []const u8, frame: u32) !Canvas {
    const src = try pipeline.acquireInput(gpa, io, path, frame);
    gpa.free(src.bytes);
    return .{
        .img = src.img,
        .fmt = src.default_fmt,
        .source = path,
        .frame = if (frame > 0) frame else null,
        .lines = .empty,
    };
}

/// Runs one block over one input, then writes whatever its @save ops asked for.
fn runBlockOn(
    gpa: std.mem.Allocator,
    io: std.Io,
    script: scriptCore.Script,
    block: scriptCore.Block,
    input: []const u8,
    opts: args.Options,
    saved: *usize,
) !void {
    var canvas = try openInput(gpa, io, input, block.frame);
    defer canvas.deinit(gpa);

    var i: u32 = block.op_start;
    const end = block.op_start + block.op_count;
    while (i < end) : (i += 1) {
        const op = script.op(i) orelse continue;
        switch (op.kind) {
            .open => {},
            .frame => {
                const n: u32 = @intFromFloat(script.opNum(i, 0) orelse 0);
                if (!video.looksLikeVideo(input)) {
                    report.err("{s}: @frame needs a video source\n", .{input});
                    return Error.FrameNeedsVideo;
                }
                canvas.deinit(gpa);
                canvas = try openInput(gpa, io, input, n);
                canvas.frame = n;
            },
            .save => {
                const target = script.opStr(i, 0);
                try apply.flushLines(gpa, &canvas.img, canvas.lines.items);
                canvas.lines.clearRetainingCapacity();
                const path = try save_mod.resolveTarget(gpa, target, canvas.source, canvas.frame, canvas.fmt);
                defer gpa.free(path);
                try save_mod.guard(path, opts.confine_output);
                const label = page_mod.pageLabelAlloc(gpa, "", 0, 0, canvas.img.width, canvas.img.height) catch null;
                defer if (label) |l| gpa.free(l);
                try pipeline.writeOutputLabeled(gpa, io, canvas.img, path, canvas.fmt, label orelse "");
                saved.* += 1;
            },
            else => try apply.applyOp(gpa, io, script, i, op, &canvas.img, &canvas.lines),
        }
    }
}

pub fn run(gpa: std.mem.Allocator, io: std.Io, opts: args.Options, path: []const u8) !void {
    const source = try load.readScript(gpa, io, path);
    defer gpa.free(source);

    var script = scriptCore.Script.parse(source) catch return load.Error.ScriptUnreadable;
    defer script.deinit();
    if (load.reportDiagnostics(script, load.labelFor(path))) return load.Error.ScriptHasErrors;

    var saved: usize = 0;
    var b: u32 = 0;
    while (b < script.blockCount()) : (b += 1) {
        const block = script.block(b) orelse continue;

        if (block.kind == .project) {
            const input = opts.input orelse {
                report.err("this script has no @source block — pass -i <path|url>\n", .{});
                return Error.NoScriptInput;
            };
            try runBlockOn(gpa, io, script, block, input, opts, &saved);
            continue;
        }

        const inputs = try sources.expand(gpa, io, block.source, block.kind);
        defer sources.freeInputs(gpa, inputs);
        if (inputs.len == 0) report.note("{s}: no files matched\n", .{block.source});
        for (inputs) |input| try runBlockOn(gpa, io, script, block, input, opts, &saved);
    }

    if (saved == 0) report.note("the script saved nothing — add a @save\n", .{});
}

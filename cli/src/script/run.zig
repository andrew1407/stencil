//! `--script <file>`: walk the lowered op stream, one block at a time, over every input
//! that block names. The core decided WHAT to do; this decides what that means for a file.
const std = @import("std");

const args = @import("../args.zig");
const image = @import("../image.zig");
const page_mod = @import("../page.zig");
const pipeline = @import("../pipeline.zig");
const report = @import("../report.zig");
const scriptCore = @import("../scriptCore.zig");
const video = @import("../video.zig");

const apply = @import("apply.zig");
const decode = @import("decode.zig");
const load = @import("load.zig");
const save_mod = @import("save.zig");
const sources = @import("sources.zig");

pub const Error = error{ NoScriptInput, FrameNeedsVideo };

/// The editing state one input carries through a block.
const Canvas = struct {
    img: image.Rgba8,
    fmt: image.Format,
    source: []const u8,
    frame: u32 = 0, // 0 = however the source opens; a @frame names one explicitly
    marks: apply.Marks,
    /// The edit ops applied so far and how many of them the pixels hold right now. There is no history
    /// stack, so an `@undo` moves the cursor and `rewind` replays the survivors — stc-contract §7.
    edits: std.ArrayList(u32) = .empty,
    cursor: usize = 0,

    fn deinit(self: *Canvas, gpa: std.mem.Allocator) void {
        self.img.deinit(gpa);
        self.marks.deinit();
        self.edits.deinit(gpa);
    }

    fn record(self: *Canvas, gpa: std.mem.Allocator, index: u32) !void {
        self.edits.shrinkRetainingCapacity(self.cursor); // a new edit drops the undone tail
        try self.edits.append(gpa, index);
        self.cursor += 1;
    }
};

fn openInput(gpa: std.mem.Allocator, io: std.Io, path: []const u8, frame: u32) !Canvas {
    const src = try pipeline.acquireInput(gpa, io, path, frame);
    gpa.free(src.bytes);
    return .{
        .img = src.img,
        .fmt = src.default_fmt,
        .source = path,
        .frame = frame,
        .marks = .init(gpa),
    };
}

/// Re-opens the input and replays the edits the cursor still covers. The fresh image is
/// acquired BEFORE the old one is released, so a failed re-open leaves the canvas intact.
fn rewind(gpa: std.mem.Allocator, io: std.Io, script: scriptCore.Script, canvas: *Canvas) !void {
    const src = try pipeline.acquireInput(gpa, io, canvas.source, canvas.frame);
    gpa.free(src.bytes);
    canvas.img.deinit(gpa);
    canvas.img = src.img;
    canvas.fmt = src.default_fmt;
    canvas.marks.clear();
    for (canvas.edits.items[0..canvas.cursor]) |index| {
        const op = script.op(index) orelse continue;
        try apply.applyOp(gpa, io, script, index, op, &canvas.img, &canvas.marks);
    }
}

fn writeSave(gpa: std.mem.Allocator, io: std.Io, canvas: *Canvas, target: []const u8, opts: args.Options) !void {
    canvas.marks.burn(&canvas.img);
    const path = try save_mod.resolveTarget(gpa, target, canvas.source, save_mod.frameOf(canvas.frame), canvas.fmt);
    defer gpa.free(path);
    save_mod.guard(path, opts.confine_output) catch |e| {
        report.err("{s}: {s}\n", .{ path, switch (e) {
            save_mod.Error.SaveTraversal => "a @save may not climb out with ..",
            save_mod.Error.SaveOutsideCwd => "--confine-output keeps every @save inside the working directory",
        } });
        return e;
    };
    const label = page_mod.pageLabelAlloc(gpa, "", 0, 0, canvas.img.width, canvas.img.height) catch null;
    defer if (label) |l| gpa.free(l);
    try pipeline.writeOutputLabeled(gpa, io, canvas.img, path, canvas.fmt, label orelse "");
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
        var buf: scriptCore.ResolveBuf = undefined;
        switch (op.kind) {
            .open => {},
            .frame => {
                if (!video.looksLikeVideo(input)) {
                    report.err("{s}: @frame needs a video source\n", .{input});
                    return Error.FrameNeedsVideo;
                }
                const n = decode.decode(script, i, .frame, 0, 0, &buf).?.frame;
                canvas.frame = n;
                canvas.edits.clearRetainingCapacity(); // §7: a new frame starts a fresh set
                canvas.cursor = 0;
                try rewind(gpa, io, script, &canvas);
            },
            .save => {
                const target = decode.decode(script, i, .save, 0, 0, &buf).?.save;
                try writeSave(gpa, io, &canvas, target, opts);
                saved.* += 1;
            },
            .undo, .redo => {
                const steps = decode.decode(script, i, op.kind, 0, 0, &buf).?.steps;
                canvas.cursor = if (op.kind == .undo)
                    canvas.cursor -| steps
                else
                    @min(canvas.cursor + steps, canvas.edits.items.len);
                try rewind(gpa, io, script, &canvas);
            },
            else => {
                try apply.applyOp(gpa, io, script, i, op, &canvas.img, &canvas.marks);
                try canvas.record(gpa, i);
            },
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

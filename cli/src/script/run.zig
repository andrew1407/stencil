//! `--script <file>`: walk the lowered op stream, one block at a time, over every input
//! that block names. The core decided WHAT to do; this decides what that means for a file.
const std = @import("std");

const args = @import("../args.zig");
const image = @import("../media/image.zig");
const page_mod = @import("../media/page.zig");
const pipeline = @import("../pipeline.zig");
const report = @import("../app/report.zig");
const scriptCore = @import("core.zig");
const video = @import("../media/video.zig");

const apply = @import("apply.zig");
const decode = @import("decode.zig");
const load = @import("load.zig");
const save_mod = @import("save.zig");
const sources = @import("sources.zig");

pub const Error = error{ NoScriptInput, FrameNeedsVideo };

/// The editing state one input carries through a block. The input is decoded once per frame
/// into `original`; a rewind copies it back instead of fetching or decoding the input again.
const Canvas = struct {
    source: []const u8,
    frame: u32 = 0, // 0 = however the source opens; a @frame names one explicitly
    original: ?image.Rgba8 = null, // null until an op first needs pixels, and again after a @frame
    img: ?image.Rgba8 = null,
    fmt: image.Format = .png,
    marks: apply.Marks,
    /// The edit ops applied so far and how many of them the pixels hold right now. There is no history
    /// stack, so an `@undo` moves the cursor and `rewind` replays the survivors — stc-contract §7.
    edits: std.ArrayList(u32) = .empty,
    cursor: usize = 0,

    fn deinit(self: *Canvas, gpa: std.mem.Allocator) void {
        if (self.original) |*o| o.deinit(gpa);
        if (self.img) |*i| i.deinit(gpa);
        self.marks.deinit();
        self.edits.deinit(gpa);
    }

    fn record(self: *Canvas, gpa: std.mem.Allocator, index: u32) !void {
        self.edits.shrinkRetainingCapacity(self.cursor); // a new edit drops the undone tail
        try self.edits.append(gpa, index);
        self.cursor += 1;
    }

    /// The working pixels, decoding the input's current frame the first time they are needed.
    fn pixels(self: *Canvas, gpa: std.mem.Allocator, io: std.Io) !*image.Rgba8 {
        if (self.original == null) {
            const src = try pipeline.acquireInput(gpa, io, self.source, self.frame);
            gpa.free(src.bytes);
            var fresh = src.img;
            errdefer fresh.deinit(gpa);
            const copy = try gpa.dupe(u8, fresh.pixels);
            if (self.img) |*i| i.deinit(gpa);
            self.img = .{ .width = fresh.width, .height = fresh.height, .pixels = copy };
            self.original = fresh;
            self.fmt = src.default_fmt;
            self.marks.clear();
        }
        return &self.img.?;
    }

    /// A @frame starts a fresh set of edits on another frame, decoded when first needed.
    fn selectFrame(self: *Canvas, gpa: std.mem.Allocator, frame: u32) void {
        if (self.original) |*o| o.deinit(gpa);
        self.original = null;
        self.frame = frame;
        self.edits.clearRetainingCapacity(); // §7: a new frame starts a fresh set
        self.cursor = 0;
    }
};

/// Copies the decoded original back over the working pixels and replays the edits the cursor
/// still covers. A crop changed the size, so the buffer is re-made to the original's first.
fn rewind(
    gpa: std.mem.Allocator,
    io: std.Io,
    script: scriptCore.Script,
    canvas: *Canvas,
    layouts: *apply.Layouts,
) !void {
    const img = try canvas.pixels(gpa, io);
    const o = canvas.original.?;
    if (img.width != o.width or img.height != o.height or img.pixels.len != o.pixels.len) {
        const px = try gpa.alloc(u8, o.pixels.len);
        img.deinit(gpa);
        img.* = .{ .width = o.width, .height = o.height, .pixels = px };
    }
    @memcpy(img.pixels, o.pixels);
    canvas.marks.clear();
    for (canvas.edits.items[0..canvas.cursor]) |index| {
        const op = script.op(index) orelse continue;
        try apply.applyOp(gpa, io, script, index, op, img, &canvas.marks, layouts);
    }
}

/// The marks land on a copy, so a later `@filter` never recolours them and a later `@crop` scales them.
fn writeSave(gpa: std.mem.Allocator, io: std.Io, canvas: *Canvas, target: []const u8, opts: args.Options, clobber: *save_mod.Clobber) !void {
    const img = try canvas.pixels(gpa, io);
    const path = try save_mod.resolveTarget(gpa, target, canvas.source, save_mod.frameOf(canvas.frame), canvas.fmt);
    defer gpa.free(path);
    save_mod.guard(io, path, opts.confine_output) catch |e| {
        report.err("{s}: {s}\n", .{ path, switch (e) {
            save_mod.Error.SaveTraversal => "a @save may not climb out with ..",
            save_mod.Error.SaveOutsideCwd => "--confine-output keeps every @save inside the working directory",
        } });
        return e;
    };
    try clobber.allow(gpa, io, path, canvas.fmt);
    const label = page_mod.pageLabelAlloc(gpa, "", 0, 0, img.width, img.height) catch null;
    defer if (label) |l| gpa.free(l);
    var drawn = try canvas.marks.render(gpa, img.*);
    defer drawn.deinit(gpa);
    try pipeline.writeOutputLabeled(gpa, io, drawn, path, canvas.fmt, label orelse "");
}

/// The block's last @save: nothing after it can reach a file, so the run stops there.
fn lastSave(script: scriptCore.Script, block: scriptCore.Block) ?u32 {
    var i = block.op_start + block.op_count;
    while (i > block.op_start) {
        i -= 1;
        const op = script.op(i) orelse continue;
        if (op.kind == .save) return i;
    }
    return null;
}

/// Runs one block over one input, up to the last @save it has.
fn runBlockOn(
    gpa: std.mem.Allocator,
    io: std.Io,
    script: scriptCore.Script,
    block: scriptCore.Block,
    input: []const u8,
    opts: args.Options,
    layouts: *apply.Layouts,
    saved: *usize,
    clobber: *save_mod.Clobber,
) !void {
    const last = lastSave(script, block) orelse return;
    var canvas: Canvas = .{ .source = input, .frame = block.frame, .marks = .init(gpa) };
    defer canvas.deinit(gpa);

    var i: u32 = block.op_start;
    while (i <= last) : (i += 1) {
        const op = script.op(i) orelse continue;
        var buf: scriptCore.ResolveBuf = undefined;
        switch (op.kind) {
            .open, .redo => {}, // a redo never reaches an adapter: the lowerer resolved it (§7)
            .frame => {
                if (!video.looksLikeVideo(input)) {
                    report.err("{s}: @frame needs a video source\n", .{input});
                    return Error.FrameNeedsVideo;
                }
                canvas.selectFrame(gpa, decode.decode(script, i, .frame, 0, 0, &buf).?.frame);
            },
            .save => {
                const target = decode.decode(script, i, .save, 0, 0, &buf).?.save;
                try writeSave(gpa, io, &canvas, target, opts, clobber);
                saved.* += 1;
            },
            .undo => {
                canvas.cursor -|= decode.decode(script, i, .undo, 0, 0, &buf).?.steps;
                try rewind(gpa, io, script, &canvas, layouts);
            },
            else => {
                const img = try canvas.pixels(gpa, io);
                try apply.applyOp(gpa, io, script, i, op, img, &canvas.marks, layouts);
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

    var clobber: save_mod.Clobber = .{ .on = opts.no_clobber };
    defer clobber.deinit(gpa);
    try clobber.precheck(gpa, io, script, opts.input, opts.confine_output);

    var saved: usize = 0;
    var layouts: apply.Layouts = .{};
    defer layouts.deinit(gpa);
    var b: u32 = 0;
    while (b < script.blockCount()) : (b += 1) {
        const block = script.block(b) orelse continue;

        if (block.kind == .project) {
            const input = opts.input orelse {
                report.err("this script has no @source block — pass -i <path|url>\n", .{});
                return Error.NoScriptInput;
            };
            try runBlockOn(gpa, io, script, block, input, opts, &layouts, &saved, &clobber);
            continue;
        }

        const inputs = try sources.expand(gpa, io, block.source, block.kind);
        defer sources.freeInputs(gpa, inputs);
        if (inputs.len == 0) report.note("{s}: no files matched\n", .{block.source});
        for (inputs) |input| try runBlockOn(gpa, io, script, block, input, opts, &layouts, &saved, &clobber);
    }

    if (saved == 0) report.note("the script saved nothing — add a @save\n", .{});
}

test "a block stops at its last @save: an op after it is never run" {
    const gpa = std.testing.allocator;
    var threaded = std.Io.Threaded.init(gpa, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();
    const stc = "stencil_run_last_save.stc";
    const out = "stencil_run_last_save.png";
    try dir.writeFile(io, .{ .sub_path = stc, .data = "@source tests/fixtures/sample.png:\n" ++
        "    @filter bw\n    @crop 10%\n    @undo\n    @save " ++ out ++ "\n    @layout no-such.json\n" });
    defer dir.deleteFile(io, stc) catch {};
    defer dir.deleteFile(io, out) catch {};
    try run(gpa, io, .{}, stc);
    try dir.access(io, out, .{});
}

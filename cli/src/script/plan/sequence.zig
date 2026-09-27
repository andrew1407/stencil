//! One block's lowered ops as the plans an executor replays. A `layout` sets the drawn lines
//! (llm-contract §2), so each carries every line that should show — the marks before it, moved
//! by the crops since — in the §1 frame its plan started in. At most MAX_ACTIONS a plan; an
//! `undo` steps the history entries actions pushed, never script edits, and ends its plan.
const std = @import("std");

const core = @import("../../core.zig");
const scriptCore = @import("../core.zig");

const actions = @import("actions.zig");
const crop_action = @import("crop.zig");
const decode = @import("../decode.zig");
const lines = @import("lines.zig");

/// A copy of the op-plan envelope's `limits.MAX_ACTIONS` (browser/js/config/llm/opRegistry.json),
/// which this layer sits below; tests/script/script_plan_test.zig pins the two together.
pub const MAX_ACTIONS: usize = 16;
/// `limits.MAX_UNDO_STEPS` of the same registry.
const MAX_STEPS: usize = 20;

/// The image size lengths resolve against; it moves as the block's crops narrow it.
pub const Dims = struct { w: f64, h: f64 };

/// Why a block cannot be planned: the op it stopped at, and the diagnostic the envelope reports.
pub const Refusal = struct { op: u32, code: []const u8, message: []const u8 };

/// What planning reads beyond the script. `remote`: the script's author is off this machine —
/// the bot, url-only by llm-contract §10 — so no local file is read, and a fetch refuses
/// loopback as well, as the bot's own guard does.
pub const Env = struct { io: std.Io, remote: bool = false, refusal: ?Refusal = null };

/// One history entry group an executor holds: the script edits it made (the newest `shapes`
/// among them), its entries, and the lines and size before it — null lines: nobody knows them.
const Entry = struct { edits: usize, steps: usize, shapes: lines.Lines = &.{}, drawn: ?lines.Lines, dims: ?Dims };

const Sequence = struct {
    a: std.mem.Allocator,
    env: *Env,
    dims: ?Dims,
    drawn: ?lines.Lines = &.{}, // what the executor shows, in view pixels
    plans: std.ArrayList([]const std.json.Value) = .empty,
    cur: std.ArrayList(std.json.Value) = .empty,
    pending: std.ArrayList(core.LineDraw) = .empty, // shapes in view pixels, not yet landed
    hist: std.ArrayList(Entry) = .empty,
    dx: f64 = 0, // the crop origins this plan has run, which an executor subtracts (§1)
    dy: f64 = 0,

    fn refuse(self: *Sequence, op: u32, code: []const u8, comptime fmt: []const u8, args: anytype) error{ PlanRefused, OutOfMemory } {
        self.env.refusal = .{ .op = op, .code = code, .message = try std.fmt.allocPrint(self.a, fmt, args) };
        return error.PlanRefused;
    }

    fn push(self: *Sequence, v: std.json.Value) !void {
        if (self.cur.items.len == MAX_ACTIONS) try self.cut();
        try self.cur.append(self.a, v);
    }

    /// Ends the plan being filled: the next is written in the frame the executor then shows.
    fn cut(self: *Sequence) !void {
        if (self.cur.items.len == 0) return;
        try self.plans.append(self.a, try self.cur.toOwnedSlice(self.a));
        self.dx = 0;
        self.dy = 0;
    }

    /// One `layout` setting the drawn lines to `set`: one entry for `edits` script edits.
    fn land(self: *Sequence, set: lines.Lines, edits: usize, shapes: lines.Lines) !void {
        if (self.cur.items.len == MAX_ACTIONS) try self.cut(); // the frame of the plan it lands in
        try self.push(try actions.linesAction(self.a, set, self.dx, self.dy));
        try self.hist.append(self.a, .{ .edits = edits, .steps = 1, .shapes = shapes, .drawn = self.drawn, .dims = self.dims });
        self.drawn = set;
    }

    /// Lands the waiting shapes over what shows. Shapes wait only where a size is known, and
    /// with it the lines drawn.
    fn flush(self: *Sequence) !void {
        if (self.pending.items.len == 0) return;
        const shapes = try self.pending.toOwnedSlice(self.a);
        try self.land(try lines.joined(self.a, self.drawn.?, shapes), shapes.len, shapes);
    }

    /// One script edit that is not a shape: its actions, one history entry each.
    fn edit(self: *Sequence, acts: []const std.json.Value) !void {
        try self.flush();
        for (acts) |v| try self.push(v);
        try self.hist.append(self.a, .{ .edits = 1, .steps = acts.len, .drawn = self.drawn, .dims = self.dims });
    }

    fn shape(self: *Sequence, op: u32, line: core.LineDraw) !void {
        if (self.drawn.?.len + self.pending.items.len >= lines.MAX_LINES)
            return self.refuse(op, "E_PLAN_TOO_MANY_LINES", "more than {d} lines would show at once, and a plan's layout carries at most {d} (limits.MAX_LAYOUT_LINES)", .{ lines.MAX_LINES, lines.MAX_LINES });
        try self.pending.append(self.a, try lines.owned(self.a, line));
    }

    /// `@undo n`: the newest edits still waiting simply never land; past them, the executor's
    /// own steps. A `layout` cut into queues its survivors again, and a new plan starts.
    fn undo(self: *Sequence, n: usize) !void {
        const waiting = @min(n, self.pending.items.len);
        self.pending.shrinkRetainingCapacity(self.pending.items.len - waiting);
        var left = n - waiting;
        if (left == 0) return;
        var steps: usize = 0;
        while (left > 0) {
            const e = self.hist.pop() orelse break;
            steps += e.steps;
            self.drawn = e.drawn;
            self.dims = e.dims;
            if (e.edits > left) try self.pending.appendSlice(self.a, e.shapes[0 .. e.edits - left]);
            left -|= e.edits;
        }
        if (steps == 0) return;
        while (steps > 0) : (steps -= @min(steps, MAX_STEPS))
            try self.push(try actions.numberAction(self.a, "undo", "steps", @intCast(@min(steps, MAX_STEPS))));
        try self.cut();
    }

    /// A fresh picture: nothing placed before it survives, and its frame starts over.
    fn restart(self: *Sequence, dims: ?Dims) void {
        self.pending.clearRetainingCapacity();
        self.hist.clearRetainingCapacity();
        self.dims = dims;
        self.drawn = &.{};
        self.dx = 0;
        self.dy = 0;
    }

    fn crop(self: *Sequence, action: std.json.Value, rect: ?core.Rect) !void {
        const before = self.dims;
        try self.edit(&.{action});
        const cur = before orelse {
            // No size to follow the crop by: what it does to the lines drawn is unknown.
            if (self.drawn) |set| if (set.len != 0) {
                self.drawn = null;
            };
            return;
        };
        // The window `--script` commits (pipeline.cropToRect's clamp), not the unrounded one.
        const r = core.snapCropRect(rect orelse return, @intFromFloat(cur.w), @intFromFloat(cur.h));
        const old: core.Rect = .{ .x = 0, .y = 0, .w = @intFromFloat(cur.w), .h = @intFromFloat(cur.h) };
        if (self.drawn) |set| self.drawn = try lines.recropped(self.a, set, old, r);
        self.dims = .{ .w = @floatFromInt(r.w), .h = @floatFromInt(r.h) };
        self.dx += @floatFromInt(r.x);
        self.dy += @floatFromInt(r.y);
    }

    /// `@layout`: the document's lines over what shows, or alone for "replace", as one layout.
    fn document(self: *Sequence, op: u32, l: decode.Layout) !void {
        if (l.kind != .url and self.env.remote)
            return self.refuse(op, "E_PLAN_LAYOUT_LOCAL", "the layout '{s}' is a local file, and the bot opens URLs only", .{l.src});
        const doc = switch (lines.document(self.a, self.env.io, l.src, l.kind == .url, self.env.remote)) {
            .lines => |set| set,
            .failed => |why| return self.refuse(op, "E_PLAN_LAYOUT_UNREADABLE", "could not load the layout '{s}': {s}", .{ l.src, why }),
        };
        try self.flush();
        const under: lines.Lines = if (std.mem.eql(u8, l.mode, "replace")) &.{} else self.drawn orelse
            return self.refuse(op, "E_PLAN_LINES_UNKNOWN", "a crop with no image size to follow moved the lines this layout lands on", .{});
        if (under.len + doc.len > lines.MAX_LINES)
            return self.refuse(op, "E_PLAN_TOO_MANY_LINES", "more than {d} lines would show at once, and a plan's layout carries at most {d} (limits.MAX_LAYOUT_LINES)", .{ lines.MAX_LINES, lines.MAX_LINES });
        try self.land(try lines.joined(self.a, under, doc), 1, &.{});
    }
};

/// The plans one block lowers to, in order. `dims` is null when nothing local could be
/// probed — the shape ops are then dropped rather than resolved against a size nobody has.
/// error.PlanRefused leaves the reason in `env.refusal`.
pub fn build(
    a: std.mem.Allocator,
    env: *Env,
    script: scriptCore.Script,
    block: scriptCore.Block,
    first_input: []const u8,
    dims: ?Dims,
) ![]const []const std.json.Value {
    var seq: Sequence = .{ .a = a, .env = env, .dims = dims };
    var i: u32 = block.op_start;
    while (i < block.op_start + block.op_count) : (i += 1) {
        const op = script.op(i) orelse continue;
        var buf: scriptCore.ResolveBuf = undefined;
        const size = seq.dims orelse Dims{ .w = 0, .h = 0 };
        const edit = decode.decode(script, i, op.kind, size.w, size.h, &buf);
        switch (op.kind) {
            .open => if (first_input.len != 0) {
                try seq.push(try actions.openAction(a, first_input, block.kind == .url));
                seq.restart(dims);
            },
            .frame => {
                try seq.push(try actions.numberAction(a, "frame", "index", edit.?.frame));
                seq.restart(dims); // --script decodes the frame afresh: the marks and edits go
            },
            .crop => try seq.crop(try crop_action.cropAction(a, script, i, seq.dims), if (edit) |e| e.crop else null),
            .filter => try seq.edit(&.{try actions.filterAction(a, edit.?.filter)}),
            // A shape with nothing to resolve against is dropped, but stays an edit an @undo counts.
            .line, .rect => if (seq.dims != null and edit != null) try seq.shape(i, edit.?.shape) else try seq.edit(&.{}),
            .layout => try seq.document(i, edit.?.layout),
            .save => {
                try seq.flush();
                try seq.push(try actions.saveAction(a, edit.?.save));
            },
            .undo => try seq.undo(edit.?.steps),
            .redo => {}, // never emitted: the lowerer resolves every @redo (§7)
        }
    }
    try seq.flush();
    try seq.cut();
    return seq.plans.items;
}

test {
    _ = actions;
    _ = crop_action;
    _ = lines;
}

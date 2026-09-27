//! The plans one block lowers to: the registry's own op names, the history an `@undo` steps
//! (a shape still waiting never lands, a `layout` cut in two lands its survivors again), the
//! §1 frame a shape after a crop is written in, and the registry's per-action caps.
const std = @import("std");
const testing = std.testing;

const scriptCore = @import("../../../src/script/core.zig");
const sequence = @import("../../../src/script/plan/sequence.zig");

const Value = std.json.Value;

/// Every plan's ops in order, `| ` between plans, a layout's line count in brackets and an
/// undo's steps after it.
fn outline(a: std.mem.Allocator, plans: []const []const Value) ![]u8 {
    var out: std.ArrayList(u8) = .empty;
    for (plans, 0..) |p, k| {
        if (k != 0) try out.appendSlice(a, "| ");
        for (p) |v| {
            try out.appendSlice(a, v.object.get("op").?.string);
            if (v.object.get("lines")) |l| try out.print(a, "[{d}]", .{l.array.items.len});
            if (v.object.get("steps")) |n| try out.print(a, "{d}", .{n.integer});
            try out.append(a, ' ');
        }
    }
    return out.items;
}

/// The actions borrow the script's strings, so the caller keeps `s` alive while it reads them.
fn lower(a: std.mem.Allocator, s: scriptCore.Script, dims: ?sequence.Dims) ![]const []const Value {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    var env: sequence.Env = .{ .io = threaded.io() };
    return sequence.build(a, &env, s, s.block(0).?, "a.png", dims);
}

const Fixture = struct {
    arena: std.heap.ArenaAllocator,
    script: scriptCore.Script,

    fn init(self: *Fixture, text: []const u8) !void {
        self.* = .{ .arena = .init(testing.allocator), .script = try scriptCore.Script.parse(text) };
    }

    fn deinit(self: *Fixture) void {
        self.script.deinit();
        self.arena.deinit();
    }

    fn plans(self: *Fixture, dims: ?sequence.Dims) ![]const []const Value {
        return lower(self.arena.allocator(), self.script, dims);
    }

    fn expectOutline(self: *Fixture, dims: ?sequence.Dims, want: []const u8) !void {
        try testing.expectEqualStrings(want, try outline(self.arena.allocator(), try self.plans(dims)));
    }
};

const sample: sequence.Dims = .{ .w = 16, .h = 12 };

test "a block lowers to the registry's own op names, shapes landing in script order" {
    var f: Fixture = undefined;
    try f.init("@source a.png:\n  @crop 10%\n  @rect (1,1) (5,5)\n  @filter aqua\n  @save out.png\n");
    defer f.deinit();
    const plans = try f.plans(.{ .w = 200, .h = 100 });
    try testing.expectEqualStrings("openFile crop layout[1] filter save ", try outline(f.arena.allocator(), plans));
    try testing.expectEqualStrings("10%", plans[0][1].object.get("spec").?.object.get("x1").?.string);
    try testing.expect(plans[0][2].object.get("lines").?.array.items[0].object.get("locked").?.bool);
    try testing.expectEqualStrings("#00ffff", plans[0][3].object.get("tint").?.string); // the tint as hex
}

test "without dims the shapes are dropped, yet an @undo still counts them" {
    var f: Fixture = undefined;
    try f.init("@source https://e.example/a.png:\n  @filter bw\n  @line (0,0) (10%,10%)\n  @undo\n  @save\n");
    defer f.deinit();
    try f.expectOutline(null, "openUrl filter save "); // the undo took the dropped line, not the filter
}

test "an @undo of shapes still waiting lands nothing; past them it steps the executor's history" {
    var f: Fixture = undefined;
    try f.init("@source a.png:\n  @filter aqua\n  @rect (1,1) (9,9)\n  @undo\n  @save\n");
    defer f.deinit();
    try f.expectOutline(sample, "openFile filter save ");

    var g: Fixture = undefined;
    try g.init("@source a.png:\n  @rect (1,1) (9,9)\n  @filter aqua\n  @undo\n  @save\n");
    defer g.deinit();
    try g.expectOutline(sample, "openFile layout[1] filter undo1 | save ");
}

test "an @undo that cuts into a layout lands its survivors again, in a new plan" {
    var f: Fixture = undefined;
    try f.init("@source a.png:\n  @line (0,0) (4,4)\n  @line (1,1) (5,5)\n  @filter bw\n  @undo 2\n  @save\n");
    defer f.deinit();
    try f.expectOutline(sample, "openFile layout[2] filter undo2 | layout[1] filter save ");
}

test "a shape after a crop is written in the plan's frame, beside the lines the crop moved" {
    var f: Fixture = undefined;
    try f.init("@source a.png:\n  @crop 25%\n  @line (0,0) (8,6)\n  @crop 25%\n  @line (1,1) (2,2)\n  @save\n");
    defer f.deinit();
    const plans = try f.plans(sample);
    try testing.expectEqualStrings("openFile crop layout[1] crop layout[2] save ", try outline(f.arena.allocator(), plans));
    const both = plans[0][4].object.get("lines").?.array.items;
    const second = both[1].object.get("points").?.array.items;
    try testing.expectEqual(@as(i64, 7), second[0].object.get("x").?.integer); // 1 + 4 + 2
    try testing.expectEqual(@as(i64, 6), second[0].object.get("y").?.integer); // 1 + 3 + 1.5 rounded, as --script commits it
    const carried = both[0].object.get("points").?.array.items;
    try testing.expectEqual(@as(i64, 10), carried[1].object.get("x").?.integer); // 8 halved by the crop, + 4 + 2
    const first = plans[0][2].object.get("lines").?.array.items[0].object.get("points").?.array.items;
    try testing.expectEqual(@as(i64, 12), first[1].object.get("x").?.integer);
}

test "each layout carries every line that shows, and an album/portrait crop clears them" {
    var f: Fixture = undefined;
    try f.init("@source a.png:\n  @line (0,0) (4,4)\n  @filter bw\n  @line (1,1) (5,5)\n  @crop x1=0 x2=25%\n  @line (1,1) (2,2)\n  @save\n");
    defer f.deinit();
    try f.expectOutline(sample, "openFile layout[1] filter layout[2] crop layout[1] save ");
}

test "an undo past 20 steps is split, as the registry's MAX_UNDO_STEPS caps one" {
    var arena: std.heap.ArenaAllocator = .init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var text: std.ArrayList(u8) = .empty;
    try text.appendSlice(a, "@source a.png:\n");
    for (0..21) |_| try text.appendSlice(a, "  @filter bw\n");
    try text.appendSlice(a, "  @undo 1\n  @save\n");
    var s = try scriptCore.Script.parse(text.items);
    defer s.deinit();
    try testing.expect(std.mem.indexOf(u8, try outline(a, try lower(a, s, sample)), "undo20 undo1 | ") != null);
}

test "more lines at once than one layout carries refuse the block, naming the shape" {
    var arena: std.heap.ArenaAllocator = .init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var text: std.ArrayList(u8) = .empty;
    try text.appendSlice(a, "@source a.png:\n");
    for (0..201) |_| try text.appendSlice(a, "  @line (0,0) (1,1)\n");
    var s = try scriptCore.Script.parse(text.items);
    defer s.deinit();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    var env: sequence.Env = .{ .io = threaded.io() };
    try testing.expectError(error.PlanRefused, sequence.build(a, &env, s, s.block(0).?, "a.png", sample));
    try testing.expectEqualStrings("E_PLAN_TOO_MANY_LINES", env.refusal.?.code);
    try testing.expectEqual(@as(u32, 202), s.op(env.refusal.?.op).?.line);
}

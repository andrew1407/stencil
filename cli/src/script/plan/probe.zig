//! The size a block's lengths resolve against: a header-only probe of a local input, or, for a
//! URL whose block draws lines, the guarded fetch `--script` opens it with, read by the same
//! header sniffer. A URL block that draws nothing is never fetched.
const std = @import("std");

const image = @import("../../media/image.zig");
const net = @import("../../net.zig");
const scriptCore = @import("../core.zig");
const video = @import("../../media/video.zig");

const Dims = @import("sequence.zig").Dims;
const Env = @import("sequence.zig").Env;

/// How much of an input's HEAD is read to find its header. Well past any SOF marker, and a
/// PREFIX rather than a capped whole-file read: a photo bigger than this still probes.
const PROBE_BYTES: usize = 4 << 20;

fn dimsOf(bytes: []const u8) ?Dims {
    const d = image.dims(bytes) orelse return null;
    return .{ .w = @floatFromInt(d.width), .h = @floatFromInt(d.height) };
}

fn local(gpa: std.mem.Allocator, io: std.Io, input: []const u8) ?Dims {
    var file = std.Io.Dir.cwd().openFile(io, input, .{}) catch return null;
    defer file.close(io);
    const head = gpa.alloc(u8, PROBE_BYTES) catch return null;
    defer gpa.free(head);
    var buf: [4096]u8 = undefined;
    var reader = file.readerStreaming(io, &buf);
    const n = reader.interface.readSliceShort(head) catch return null;
    return dimsOf(head[0..n]);
}

/// The first op that puts lines on the picture — a shape or a layout document — if any.
fn firstDraw(script: scriptCore.Script, block: scriptCore.Block) ?u32 {
    var i: u32 = block.op_start;
    while (i < block.op_start + block.op_count) : (i += 1) {
        const op = script.op(i) orelse continue;
        if (op.kind == .line or op.kind == .rect or op.kind == .layout) return i;
    }
    return null;
}

/// `input`'s size, or null for a video, a local file with no readable header, or a URL block
/// that draws nothing. A URL that must be sized and does not fetch refuses the block.
pub fn size(a: std.mem.Allocator, env: *Env, script: scriptCore.Script, block: scriptCore.Block, input: []const u8) !?Dims {
    if (input.len == 0 or video.looksLikeVideo(input)) return null;
    if (!net.isUrl(input)) return local(a, env.io, input);
    const op = firstDraw(script, block) orelse return null;
    const bytes = net.fetch(a, env.io, input, env.remote) catch null;
    if (bytes) |b| if (dimsOf(b)) |d| return d;
    env.refusal = .{ .op = op, .code = "E_PLAN_SOURCE_UNREADABLE", .message = try std.fmt.allocPrint(a, "the lines this block draws need the size of '{s}', which could not be fetched as an image", .{input}) };
    return error.PlanRefused;
}

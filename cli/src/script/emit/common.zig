//! The pieces both emit backends build a target call from: string literals, a shape's point
//! tokens and a history step count. Twin of the per-op reading in browser/js/core/script.js.
const std = @import("std");

const scriptCore = @import("../../scriptCore.zig");

pub const Error = error{EmitUnsupported};

/// Why a target cannot carry a directive, with the span that named it. Carries its own text
/// so a backend needs no allocator; emit.zig prints it as the CLI's one error line.
pub const Refusal = struct {
    line: u32 = 0,
    col: u32 = 0,
    buf: [256]u8 = undefined,
    len: usize = 0,

    pub fn set(self: *Refusal, line: u32, col: u32, comptime fmt: []const u8, args: anytype) Error {
        self.line = line;
        self.col = col;
        const written = std.fmt.bufPrint(&self.buf, fmt, args) catch self.buf[0..0];
        self.len = written.len;
        return Error.EmitUnsupported;
    }

    pub fn text(self: *const Refusal) []const u8 {
        return self.buf[0..self.len];
    }
};

/// A target's string literal. Only the delimiter, a backslash and the two line breaks need
/// escaping — the lexer never lets another control byte into a token.
pub fn writeQuoted(out: *std.Io.Writer, quote: u8, text: []const u8) !void {
    try out.writeByte(quote);
    for (text) |ch| {
        switch (ch) {
            '\\' => try out.writeAll("\\\\"),
            '\n' => try out.writeAll("\\n"),
            '\r' => try out.writeAll("\\r"),
            else => {
                if (ch == quote) try out.writeByte('\\');
                try out.writeByte(ch);
            },
        }
    }
    try out.writeByte(quote);
}

/// A number as the targets spell it: an integral value without its `.0` tail.
pub fn writeNumber(out: *std.Io.Writer, value: f64) !void {
    if (value == @round(value) and @abs(value) < 1e15) {
        try out.print("{d}", .{@as(i64, @intFromFloat(value))});
    } else {
        try out.print("{d}", .{value});
    }
}

pub const Point = struct { x: []const u8, y: []const u8 };

/// How many points the emitted shape carries: a two-point @rect draws its four corners,
/// exactly as core's resolveShape expands it.
pub fn pointCount(script: scriptCore.Script, index: u32, op: scriptCore.Op) u32 {
    const pairs = script.opTokCount(index) / 2;
    return if (op.kind == .rect and pairs == 2) 4 else pairs;
}

/// Point `k`, as the two UNRESOLVED length tokens the script wrote ("10%", "-1in").
pub fn pointAt(script: scriptCore.Script, index: u32, op: scriptCore.Op, k: u32) Point {
    const pairs = script.opTokCount(index) / 2;
    if (op.kind == .rect and pairs == 2) {
        const xs: [4]u32 = .{ 0, 2, 2, 0 };
        const ys: [4]u32 = .{ 1, 1, 3, 3 };
        return .{ .x = script.opTok(index, xs[k]), .y = script.opTok(index, ys[k]) };
    }
    return .{ .x = script.opTok(index, k * 2), .y = script.opTok(index, k * 2 + 1) };
}

/// `@undo`/`@redo` carry their count in nums{0}; both runners floor it at one step.
pub fn steps(script: scriptCore.Script, index: u32) u32 {
    const n = script.opNum(index, 0) orelse 1;
    if (!(n > 1)) return 1;
    return @intFromFloat(@round(n));
}

/// The crop spec, in the toks{x1,x2,y1,y2} order cropSpecOf reads it.
pub const Crop = struct {
    pub const keys: [4][]const u8 = .{ "x1", "x2", "y1", "y2" };
    toks: [4][]const u8,
    aspect: []const u8,
    album: bool,
};

pub fn cropOf(script: scriptCore.Script, index: u32) Crop {
    return .{
        .toks = .{
            script.opTok(index, 0), script.opTok(index, 1),
            script.opTok(index, 2), script.opTok(index, 3),
        },
        .aspect = script.opStr(index, 0),
        .album = (script.opNum(index, 0) orelse 0) != 0,
    };
}

const testing = std.testing;

test "a quoted literal escapes only the delimiter and the backslash" {
    var buf: [64]u8 = undefined;
    var out: std.Io.Writer = .fixed(&buf);
    try writeQuoted(&out, '\'', "it's a \\ path");
    try testing.expectEqualStrings("'it\\'s a \\\\ path'", out.buffered());
}

test "a two-point rect emits its four corners in draw order" {
    var s = try scriptCore.Script.parse("@source a.png:\n  @rect (1, 2) (3, 4)\n  @save o.png\n");
    defer s.deinit();
    const op = s.op(1).?;
    try testing.expectEqual(@as(u32, 4), pointCount(s, 1, op));
    try testing.expectEqualStrings("1px", pointAt(s, 1, op, 0).x);
    try testing.expectEqualStrings("3px", pointAt(s, 1, op, 1).x);
    try testing.expectEqualStrings("2px", pointAt(s, 1, op, 1).y);
    try testing.expectEqualStrings("4px", pointAt(s, 1, op, 3).y);
}

test "a crop keeps its edge tokens unresolved" {
    var s = try scriptCore.Script.parse("@source a.png:\n  @crop 10%\n  @save o.png\n");
    defer s.deinit();
    const crop = cropOf(s, 1);
    try testing.expectEqualStrings("10%", crop.toks[0]);
    try testing.expectEqualStrings("-10%", crop.toks[1]);
    try testing.expect(!crop.album);
}

test "a number drops its fractional tail only when it has none" {
    var buf: [32]u8 = undefined;
    var out: std.Io.Writer = .fixed(&buf);
    try writeNumber(&out, 3);
    try out.writeByte(' ');
    try writeNumber(&out, 2.5);
    try testing.expectEqualStrings("3 2.5", out.buffered());
}

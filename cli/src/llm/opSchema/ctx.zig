//! Message plumbing for the schema checks: the first-failure collector and the
//! walking context, plus the `"x1" in spec` / `ask.options[2]` path wording.
const std = @import("std");
const json = @import("json.zig");
const Schema = @import("../opSchema.zig").Schema;

const Value = json.Value;
const ObjectMap = json.ObjectMap;
pub const Error = json.Error;

/// Collects the first failure's user-facing message (gpa-owned). `prefix` is the entry
/// point's ("invalid crop action: " / "invalid plan: ").
pub const Diag = struct {
    gpa: std.mem.Allocator,
    msg: ?[]u8 = null,
    prefix: []const u8 = "",

    pub fn fail(self: *Diag, comptime fmt: []const u8, args: anytype) Error {
        if (self.msg == null) self.msg = try std.fmt.allocPrint(self.gpa, "{s}" ++ fmt, .{self.prefix} ++ args);
        return error.Invalid;
    }

    pub fn deinit(self: *Diag) void {
        if (self.msg) |m| self.gpa.free(m);
        self.msg = null;
    }
};

pub const Path = struct { root: []const u8 = "", key: []const u8 = "", container: ?[]const u8 = null };

pub const Ctx = struct {
    a: std.mem.Allocator, // scratch for messages and folded copies
    diag: *Diag,
    schema: *const Schema,

    pub fn fail(self: Ctx, comptime fmt: []const u8, args: anytype) Error {
        return self.diag.fail(fmt, args);
    }
};

/// `spec`, `ask.options[2]`, `lines[0].points[3]`.
pub fn where(ctx: Ctx, p: Path) Error![]const u8 {
    if (p.key.len == 0) return std.mem.trimEnd(u8, p.root, ".");
    return std.fmt.allocPrint(ctx.a, "{s}{s}{s}{s}", .{ p.container orelse "", if (p.container != null) "." else "", p.root, p.key });
}

/// `"x1" in spec`, `"ask.question"`.
pub fn label(ctx: Ctx, p: Path) Error![]const u8 {
    if (p.container) |c| return std.fmt.allocPrint(ctx.a, "\"{s}{s}\" in {s}", .{ p.root, p.key, c });
    return std.fmt.allocPrint(ctx.a, "\"{s}{s}\"", .{ p.root, p.key });
}

pub fn child(ctx: Ctx, p: ?Path, key: []const u8) Error!Path {
    if (p) |parent| {
        if (parent.key.len != 0) return .{ .key = key, .container = try where(ctx, parent) };
        return .{ .root = parent.root, .key = key };
    }
    return .{ .key = key };
}

pub fn item(ctx: Ctx, p: Path, i: usize) Error!Path {
    return .{ .root = p.root, .key = try std.fmt.allocPrint(ctx.a, "{s}[{d}]", .{ p.key, i }), .container = p.container };
}

pub fn numText(ctx: Ctx, v: Value) Error![]const u8 {
    return switch (v) {
        .integer => |i| std.fmt.allocPrint(ctx.a, "{d}", .{i}),
        .float => |f| std.fmt.allocPrint(ctx.a, "{d}", .{f}),
        .number_string => |s| s,
        else => "?",
    };
}

/// `"a", "b"` / `1, 2` / `true`.
pub fn quoteList(ctx: Ctx, list: Value) Error![]const u8 {
    var out: std.ArrayList(u8) = .empty;
    for (list.array.items, 0..) |x, i| {
        if (i != 0) try out.appendSlice(ctx.a, ", ");
        switch (x) {
            .string => |s| try out.print(ctx.a, "\"{s}\"", .{s}),
            .bool => |b| try out.appendSlice(ctx.a, if (b) "true" else "false"),
            else => try out.appendSlice(ctx.a, try numText(ctx, x)),
        }
    }
    return out.toOwnedSlice(ctx.a);
}

pub fn quoteKeys(ctx: Ctx, keys: []const Value, sep: []const u8) Error![]const u8 {
    var out: std.ArrayList(u8) = .empty;
    for (keys, 0..) |k, i| {
        if (i != 0) try out.appendSlice(ctx.a, sep);
        try out.print(ctx.a, "\"{s}\"", .{k.string});
    }
    return out.toOwnedSlice(ctx.a);
}

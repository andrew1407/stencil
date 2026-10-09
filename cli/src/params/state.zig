//! The cursor the flag parsers advance over argv and the value readers they share: the loop
//! is parse.zig, and a feature with a block of flags of its own (scrape, inspect, blank)
//! reads them in its own file through these.
const std = @import("std");
const logo = @import("../app/logo.zig");
const core = @import("../core.zig");
const options = @import("options.zig");

const Error = options.Error;

pub const ParseState = struct {
    argv: []const [:0]const u8,
    i: usize,

    pub fn next(self: *ParseState) ?[:0]const u8 {
        if (self.i >= self.argv.len) return null;
        const v = self.argv[self.i];
        self.i += 1;
        return v;
    }
};

pub fn value(st: *ParseState, flag: []const u8) Error![:0]const u8 {
    return st.next() orelse {
        logo.err("{s} expects a value\n", .{flag});
        return Error.MissingValue;
    };
}

/// `flag`'s value as a whole number; the refusal names the flag and what it got.
pub fn parseU32(flag: []const u8, s: []const u8) Error!u32 {
    return std.fmt.parseInt(u32, s, 10) catch return badNumber(flag, s);
}

pub fn parseI32(flag: []const u8, s: []const u8) Error!i32 {
    return std.fmt.parseInt(i32, s, 10) catch return badNumber(flag, s);
}

fn badNumber(flag: []const u8, s: []const u8) Error {
    logo.err("{s} expects a whole number, got '{s}'\n", .{ flag, s });
    return Error.BadNumber;
}

/// A `--filter` mode, refused unless core reads it as a named filter or a colour.
pub fn filter(v: []const u8) Error![]const u8 {
    if (core.isFilter(v)) return v;
    logo.err("--filter expects bw, invert, contour, sepia, a colour name or #hex, got '{s}'\n", .{v});
    return Error.BadValue;
}

pub fn eq(a: []const u8, b: []const u8) bool {
    return std.mem.eql(u8, a, b);
}

test "parseU32/parseI32: a whole number passes; anything else is BadNumber" {
    try std.testing.expectEqual(@as(u32, 7), try parseU32("--frame", "7"));
    try std.testing.expectEqual(@as(i32, -1), try parseI32("--rotate", "-1"));
    for ([_][]const u8{ "1.5", "", "x", "-1" }) |bad| try std.testing.expectError(Error.BadNumber, parseU32("--frame", bad));
    try std.testing.expectError(Error.BadNumber, parseI32("--rotate", "1.5"));
}

test "filter: a named mode in any case or a colour passes; anything else is refused" {
    for ([_][]const u8{ "bw", "Sepia", "invert", "contour", "none", "red", "#7c3aed", "#abc" }) |ok| {
        try std.testing.expectEqualStrings(ok, try filter(ok));
    }
    for ([_][]const u8{ "", "#ggg", "notacolor", "bw2" }) |bad| try std.testing.expectError(Error.BadValue, filter(bad));
}

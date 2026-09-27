//! The cursor the flag parsers advance over argv and the value readers they share: the loop
//! is parse.zig, and a feature with a block of flags of its own (scrape, inspect, blank)
//! reads them in its own file through these.
const std = @import("std");
const logo = @import("../app/logo.zig");
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

pub fn parseU32(s: []const u8) Error!u32 {
    return std.fmt.parseInt(u32, s, 10) catch return Error.BadNumber;
}

pub fn parseI32(s: []const u8) Error!i32 {
    return std.fmt.parseInt(i32, s, 10) catch return Error.BadNumber;
}

pub fn eq(a: []const u8, b: []const u8) bool {
    return std.mem.eql(u8, a, b);
}

//! The one reader for a report mode's document argument (`--plan-check`, `--merge-lines`): a file
//! under the working directory, or stdin for "-", held to one byte cap. Each refusal says which
//! document it was, and the caller exits 2 (CONTRACT.md §7, §8).
const std = @import("std");
const confine = @import("confine.zig");
const report = @import("../app/report.zig");

pub const MAX_BYTES: usize = 8 << 20;

/// The document could not be had: no file, a `..` climb, over the cap. Already reported.
pub const Error = error{ Unreadable, OutOfMemory };

/// `path`'s bytes, caller-owned; `noun` names the document in a refusal ("reply").
pub fn read(gpa: std.mem.Allocator, io: std.Io, path: []const u8, noun: []const u8) Error![]u8 {
    if (std.mem.eql(u8, path, "-")) {
        var buf: [4096]u8 = undefined;
        var stdin = std.Io.File.stdin().readerStreaming(io, &buf);
        return stdin.interface.allocRemaining(gpa, .limited(MAX_BYTES)) catch |e| return failed("<stdin>", noun, e);
    }
    if (confine.hasParentTraversal(path)) {
        report.err("refusing to read a {s} that climbs out of the working directory: '{s}'\n", .{ noun, path });
        return Error.Unreadable;
    }
    return std.Io.Dir.cwd().readFileAlloc(io, path, gpa, .limited(MAX_BYTES)) catch |e| failed(path, noun, e);
}

fn failed(label: []const u8, noun: []const u8, e: anyerror) Error {
    if (e == error.OutOfMemory) return error.OutOfMemory;
    if (e == error.StreamTooLong) {
        report.err("that {s} is too large: {s} (the cap is {d} bytes)\n", .{ noun, label, MAX_BYTES });
    } else {
        report.err("cannot read the {s} {s} ({s})\n", .{ noun, label, @errorName(e) });
    }
    return Error.Unreadable;
}

test "read refuses a climb and a missing file, each as Unreadable" {
    var threaded = std.Io.Threaded.init(std.testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    try std.testing.expectError(Error.Unreadable, read(std.testing.allocator, io, "../x.json", "reply"));
    try std.testing.expectError(Error.Unreadable, read(std.testing.allocator, io, "stencil_no_such_input.json", "reply"));
}

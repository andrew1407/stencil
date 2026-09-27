//! The pixels a script run wrote, read back for the suites that compare them.
const std = @import("std");
const image = @import("../../src/media/image.zig");

/// The decoded pixels of a file the runner just wrote.
pub fn pixelsOf(gpa: std.mem.Allocator, io: std.Io, path: []const u8) ![]u8 {
    const bytes = try std.Io.Dir.cwd().readFileAlloc(io, path, gpa, .limited(4 << 20));
    defer gpa.free(bytes);
    var img = try image.decode(gpa, bytes);
    defer img.deinit(gpa);
    return gpa.dupe(u8, img.pixels);
}

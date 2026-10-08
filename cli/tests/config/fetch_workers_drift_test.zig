//! Drift guard for the scrape pool's width: net/fetchPool.zig's `max_workers` is a port of
//! pystencil's `MAX_FETCH_WORKERS` (pystencil/pystencil/_net.py), a Python constant no build
//! step can embed, so this reads the Python source and pins the two together.
const std = @import("std");
const fetchPool = @import("../../src/net/fetchPool.zig");
const testing = std.testing;

/// The repo root, found by walking up from cwd (cli/ under `zig build test`) to CLAUDE.md.
fn openRepoRoot(io: std.Io) !std.Io.Dir {
    for ([_][]const u8{ ".", "..", "../..", "../../.." }) |rel| {
        var dir = std.Io.Dir.cwd().openDir(io, rel, .{}) catch continue;
        if (dir.access(io, "CLAUDE.md", .{})) |_| return dir else |_| dir.close(io);
    }
    return error.RepoRootNotFound;
}

test "fetchPool.max_workers is pystencil's MAX_FETCH_WORKERS" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var root = try openRepoRoot(io);
    defer root.close(io);
    const src = try root.readFileAlloc(io, "pystencil/pystencil/_net.py", a, .limited(1 << 20));
    defer a.free(src);

    const needle = "\nMAX_FETCH_WORKERS = ";
    const at = (std.mem.indexOf(u8, src, needle) orelse return error.ConstantNotFound) + needle.len;
    const end = std.mem.indexOfScalarPos(u8, src, at, '\n') orelse src.len;
    const value = try std.fmt.parseInt(usize, std.mem.trim(u8, src[at..end], " \r"), 10);
    try testing.expectEqual(value, fetchPool.max_workers);
}

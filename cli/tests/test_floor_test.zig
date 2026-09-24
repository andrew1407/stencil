//! Test-count floor for the cli surface: a static scan of every column-0 `test "` / `test {` declaration
//! under cli/src and cli/tests. Zig's runner has no introspection from inside a test, so the scan catches
//! a deleted or unbuilt test file, not a built test that never ran. std.fs + std.mem only.
const std = @import("std");
const testing = std.testing;

const scopes = [_][]const u8{ "cli/src", "cli/tests" };

/// Raise it when the suite grows a lot; additions must never trip it, a collapse must.
const test_floor = 365;

fn countTests(bytes: []const u8) usize {
    var n: usize = 0;
    var it = std.mem.splitScalar(u8, bytes, '\n');
    while (it.next()) |line| {
        n += @intFromBool(std.mem.startsWith(u8, line, "test \"") or std.mem.startsWith(u8, line, "test {"));
    }
    return n;
}

/// The repo root, found by walking up from cwd (cli/ under `zig build test`) to CLAUDE.md.
fn openRepoRoot(io: std.Io) !std.Io.Dir {
    for ([_][]const u8{ ".", "..", "../..", "../../.." }) |rel| {
        var dir = std.Io.Dir.cwd().openDir(io, rel, .{}) catch continue;
        if (dir.access(io, "CLAUDE.md", .{})) |_| return dir else |_| dir.close(io);
    }
    return error.RepoRootNotFound;
}

fn collect(a: std.mem.Allocator, io: std.Io, root: std.Io.Dir, scope: []const u8) !usize {
    var dir = try root.openDir(io, scope, .{ .iterate = true });
    defer dir.close(io);
    var walker = try dir.walk(a);
    defer walker.deinit();
    var declared: usize = 0;
    while (try walker.next(io)) |e| {
        if (e.kind != .file or !std.mem.endsWith(u8, e.basename, ".zig")) continue;
        const bytes = try e.dir.readFileAlloc(io, e.basename, a, .limited(8 * 1024 * 1024));
        declared += countTests(bytes);
    }
    return declared;
}

test "test floor: the cli suite still declares at least its floor of tests" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var root = try openRepoRoot(io);
    defer root.close(io);

    var declared: usize = 0;
    for (scopes) |scope| declared += try collect(a, io, root, scope);
    if (declared < test_floor) {
        std.debug.print("cli suite collapsed to {d} tests, floor is {d}\n", .{ declared, test_floor });
        return error.TestSuiteCollapsed;
    }
}

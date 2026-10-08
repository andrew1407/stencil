//! One registration convention for the whole tree: a package root lists its package's files in
//! a `test { _ = @import("…"); }` block, and main.zig lists the top-level modules — else a new
//! file compiles, is reachable through an alias, and has no tests run. This lint walks src/ for
//! any file no `test {}` block names, and tests/ for any suite test_root.zig does not name.
const std = @import("std");
const testing = std.testing;

/// Roots of their own build: main.zig is the test root, bench.zig is the `zig build bench` exe.
const roots = [_][]const u8{ "main.zig", "bench/bench.zig" };

fn openSrc(io: std.Io) !std.Io.Dir {
    return std.Io.Dir.cwd().openDir(io, "src", .{ .iterate = true }) catch
        std.Io.Dir.cwd().openDir(io, "cli/src", .{ .iterate = true });
}

/// Join a file's directory with an import path, resolving the leading `../` hops.
fn resolve(a: std.mem.Allocator, from: []const u8, rel: []const u8) ![]u8 {
    var dir = if (std.mem.lastIndexOfScalar(u8, from, '/')) |s| from[0..s] else "";
    var path = rel;
    while (std.mem.startsWith(u8, path, "../")) {
        path = path[3..];
        dir = if (std.mem.lastIndexOfScalar(u8, dir, '/')) |s| dir[0..s] else "";
    }
    if (dir.len == 0) return a.dupe(u8, path);
    return std.fmt.allocPrint(a, "{s}/{s}", .{ dir, path });
}

test "test registration: every src module is pulled into the test build by a test {} block" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var src = try openSrc(io);
    defer src.close(io);

    var files: std.ArrayList([]const u8) = .empty;
    var registered = std.StringHashMap(void).init(a);
    var walker = try src.walk(a);
    defer walker.deinit();
    while (try walker.next(io)) |e| {
        if (e.kind != .file or !std.mem.endsWith(u8, e.basename, ".zig")) continue;
        const rel = try a.dupe(u8, e.path);
        std.mem.replaceScalar(u8, rel, '\\', '/'); // walker paths are host-separated
        try files.append(a, rel);
        const bytes = try e.dir.readFileAlloc(io, e.basename, a, .limited(4 << 20));
        // Both registration spellings: `_ = @import("x.zig");` and `_ = name;` where `name`
        // is a container-level `const name = @import("x.zig")`.
        var named = std.StringHashMap([]const u8).init(a);
        var it = std.mem.splitScalar(u8, bytes, '\n');
        while (it.next()) |line| {
            const t = std.mem.trim(u8, line, " \t\r");
            if (std.mem.startsWith(u8, t, "const ") or std.mem.startsWith(u8, t, "pub const ")) {
                const eq = std.mem.indexOf(u8, t, " = @import(\"") orelse continue;
                const name = t[std.mem.indexOf(u8, t, "const ").? + 6 .. eq];
                const open = eq + " = @import(\"".len;
                const close = std.mem.indexOfScalarPos(u8, t, open, '"') orelse continue;
                try named.put(name, t[open..close]);
                continue;
            }
            if (!std.mem.startsWith(u8, t, "_ = ")) continue;
            const rhs = std.mem.trim(u8, t["_ = ".len..], " ;");
            if (std.mem.startsWith(u8, rhs, "@import(\"")) {
                const open = std.mem.indexOfScalar(u8, rhs, '"').? + 1;
                const close = std.mem.indexOfScalarPos(u8, rhs, open, '"') orelse continue;
                try registered.put(try resolve(a, rel, rhs[open..close]), {});
            } else if (named.get(rhs)) |path| {
                try registered.put(try resolve(a, rel, path), {});
            }
        }
    }
    try testing.expect(files.items.len >= 40); // the tree really was walked

    var failures: usize = 0;
    outer: for (files.items) |f| {
        for (roots) |r| if (std.mem.eql(u8, f, r)) continue :outer;
        if (registered.contains(f)) continue;
        const dir = if (std.mem.lastIndexOfScalar(u8, f, '/')) |s| f[0..s] else ".";
        std.debug.print("UNREGISTERED: src/{s} — add `_ = @import(\"…\");` to {s}'s package root\n", .{ f, dir });
        failures += 1;
    }
    try testing.expectEqual(@as(usize, 0), failures);
}

test "test registration: every suite under tests/ is named in test_root.zig" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    const in_cli = if (std.Io.Dir.cwd().access(io, "test_root.zig", .{})) |_| true else |_| false;
    const root_src = try std.Io.Dir.cwd().readFileAlloc(io, if (in_cli) "test_root.zig" else "cli/test_root.zig", a, .limited(1 << 20));
    var tests = try std.Io.Dir.cwd().openDir(io, if (in_cli) "tests" else "cli/tests", .{ .iterate = true });
    defer tests.close(io);

    var walker = try tests.walk(a);
    defer walker.deinit();
    var suites: usize = 0;
    var failures: usize = 0;
    while (try walker.next(io)) |e| {
        if (e.kind != .file or !std.mem.endsWith(u8, e.basename, "_test.zig")) continue;
        const rel = try std.fmt.allocPrint(a, "\"tests/{s}\"", .{e.path});
        std.mem.replaceScalar(u8, rel, '\\', '/');
        suites += 1;
        if (std.mem.indexOf(u8, root_src, rel) != null) continue;
        std.debug.print("UNREGISTERED SUITE: {s} — name it in test_root.zig\n", .{rel});
        failures += 1;
    }
    try testing.expect(suites >= 40); // the tree really was walked
    try testing.expectEqual(@as(usize, 0), failures);
}

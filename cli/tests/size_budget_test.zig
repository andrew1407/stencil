//! Size + comment ratchet for the cli surface: walks the .zig under cli/src and
//! cli/tests and holds every file to the counts pinned in size_budget.json. Zig's
//! inline `test { }` blocks are idiomatic and not penalised, so the budget records
//! PRODUCTION lines (everything before the first column-0 `test "` / `test {`).
//! Paths are repo-relative (root = the dir holding CLAUDE.md) so the numbers read
//! the same from anywhere. std.fs + std.json only; vendored .c/.h are out of scope.
const std = @import("std");
const testing = std.testing;

const budget_json = @embedFile("size_budget.json");
const scopes = [_][]const u8{ "cli/src", "cli/tests" };

const Counts = struct { total: usize, comment: usize, prod: usize };
const Entry = struct { path: []const u8, counts: Counts };

/// Total lines, comment lines and production lines. A comment line's first
/// non-whitespace is `//` — Zig has no block comments, and a multiline string's
/// lines start with `\\`, so a `//` inside a literal never counts.
fn count(bytes: []const u8) Counts {
    var c = Counts{ .total = 0, .comment = 0, .prod = 0 };
    var body = bytes;
    if (std.mem.endsWith(u8, body, "\n")) body = body[0 .. body.len - 1];
    if (body.len == 0) return c;
    var seen_test = false;
    var it = std.mem.splitScalar(u8, body, '\n');
    while (it.next()) |line| {
        c.total += 1;
        if (std.mem.startsWith(u8, std.mem.trimStart(u8, line, " \t\r"), "//")) c.comment += 1;
        const opens = std.mem.startsWith(u8, line, "test \"") or std.mem.startsWith(u8, line, "test {");
        if (opens and !seen_test) {
            c.prod = c.total - 1;
            seen_test = true;
        }
    }
    if (!seen_test) c.prod = c.total;
    return c;
}

/// The repo root, found by walking up from cwd (cli/ under `zig build test`) to CLAUDE.md.
fn openRepoRoot(io: std.Io) !std.Io.Dir {
    for ([_][]const u8{ ".", "..", "../..", "../../.." }) |rel| {
        var dir = std.Io.Dir.cwd().openDir(io, rel, .{}) catch continue;
        if (dir.access(io, "CLAUDE.md", .{})) |_| return dir else |_| dir.close(io);
    }
    return error.RepoRootNotFound;
}

fn collect(a: std.mem.Allocator, io: std.Io, root: std.Io.Dir, scope: []const u8, out: *std.ArrayList(Entry)) !void {
    var dir = try root.openDir(io, scope, .{ .iterate = true });
    defer dir.close(io);
    var walker = try dir.walk(a);
    defer walker.deinit();
    while (try walker.next(io)) |e| {
        if (e.kind != .file or !std.mem.endsWith(u8, e.basename, ".zig")) continue;
        const bytes = try e.dir.readFileAlloc(io, e.basename, a, .limited(8 * 1024 * 1024));
        const path = try std.fmt.allocPrint(a, "{s}/{s}", .{ scope, e.path });
        std.mem.replaceScalar(u8, path, '\\', '/'); // walker paths are host-separated
        try out.append(a, .{ .path = path, .counts = count(bytes) });
    }
}

fn dirOf(path: []const u8) []const u8 {
    const slash = std.mem.lastIndexOfScalar(u8, path, '/') orelse return ".";
    return path[0..slash];
}

fn intOf(v: std.json.Value) usize {
    return @intCast(v.integer);
}

test "size budget: every cli .zig stays within its pinned production line count" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    const budget = try std.json.parseFromSliceLeaky(std.json.Value, a, budget_json, .{});
    const max = intOf(budget.object.get("maxNewFileLines").?);
    const files = budget.object.get("files").?.object;
    const exceptions = budget.object.get("exceptions").?.object;

    var root = try openRepoRoot(io);
    defer root.close(io);
    var found: std.ArrayList(Entry) = .empty;
    for (scopes) |scope| try collect(a, io, root, scope, &found);
    try testing.expect(found.items.len >= 40); // the scope really was walked

    var failures: usize = 0;
    var live = std.StringHashMap(void).init(a);
    for (found.items) |f| {
        try live.put(f.path, {});
        if (exceptions.contains(f.path)) continue; // generated / byte-pinned twins
        if (files.get(f.path)) |pinned| {
            const cap = intOf(pinned);
            if (f.counts.prod > cap) {
                std.debug.print("GREW: {s} is {d} production lines, pinned at {d}\n", .{ f.path, f.counts.prod, cap });
                failures += 1;
            } else if (f.counts.prod * 10 < cap * 9) {
                std.debug.print("note: {s} shrank to {d} lines — ratchet its budget down from {d}\n", .{ f.path, f.counts.prod, cap });
            }
        } else if (f.counts.prod > max) {
            std.debug.print("NEW OVERSIZED FILE: {s} is {d} production lines (max {d}); split it, or pin it in files\n", .{ f.path, f.counts.prod, max });
            failures += 1;
        }
    }

    var pinned_it = files.iterator();
    while (pinned_it.next()) |kv| {
        if (!live.contains(kv.key_ptr.*))
            std.debug.print("note: {s} is pinned in files but no longer exists — drop the entry\n", .{kv.key_ptr.*});
    }

    // Comment share per directory (over total lines, so inline tests count too).
    var shares = std.StringHashMap([2]usize).init(a);
    for (found.items) |f| {
        const gop = try shares.getOrPut(dirOf(f.path));
        if (!gop.found_existing) gop.value_ptr.* = .{ 0, 0 };
        gop.value_ptr.*[0] += f.counts.comment;
        gop.value_ptr.*[1] += f.counts.total;
    }
    var pct_it = budget.object.get("commentPct").?.object.iterator();
    while (pct_it.next()) |kv| {
        const tally = shares.get(kv.key_ptr.*) orelse {
            std.debug.print("note: {s} is pinned in commentPct but holds no .zig\n", .{kv.key_ptr.*});
            continue;
        };
        const pct = tally[0] * 100 / tally[1];
        const cap = intOf(kv.value_ptr.*);
        if (pct > cap) {
            std.debug.print("COMMENT SHARE ROSE: {s} is {d}% ({d}/{d}), pinned at {d}%\n", .{ kv.key_ptr.*, pct, tally[0], tally[1], cap });
            failures += 1;
        }
    }

    try testing.expectEqual(@as(usize, 0), failures);
}

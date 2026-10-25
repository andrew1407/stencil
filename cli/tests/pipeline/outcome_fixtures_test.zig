//! The stderr outcome corpus (common/fixtures/cli/outcome.json) mcp and bot parse, reproduced
//! from the CLI's side: every success line in the CLI's own shape and every server-delivery line
//! a case expects is one output.zig's and oneshot.zig's `line` formats print, and every error the
//! parsers collect carries report.err's `error: ` prefix.
const std = @import("std");
const wrote = @import("../../src/pipeline/output.zig").line;
const remote = @import("../../src/pipeline/oneshot.zig").line;
const testing = std.testing;

fn openRepoRoot(io: std.Io) !std.Io.Dir {
    for ([_][]const u8{ ".", "..", "../..", "../../.." }) |rel| {
        var dir = std.Io.Dir.cwd().openDir(io, rel, .{}) catch continue;
        if (dir.access(io, "CLAUDE.md", .{})) |_| return dir else |_| dir.close(io);
    }
    return error.RepoRootNotFound;
}

/// `fmt` with its trailing newline dropped, as a line of a case's stderr.
fn render(a: std.mem.Allocator, comptime fmt: []const u8, args: anytype) ![]const u8 {
    const s = try std.fmt.allocPrint(a, fmt, args);
    return std.mem.trimEnd(u8, s, "\n");
}

fn hasLine(stderr: []const u8, want: []const u8) bool {
    var it = std.mem.splitScalar(u8, stderr, '\n');
    while (it.next()) |l| if (std.mem.eql(u8, std.mem.trimEnd(u8, l, "\r"), want)) return true;
    return false;
}

fn int(v: std.json.Value) i64 {
    return v.integer;
}

test "every outcome fixture line the CLI writes is one its formats print" {
    var arena_state = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena_state.deinit();
    const a = arena_state.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var root = try openRepoRoot(io);
    defer root.close(io);
    const text = try root.readFileAlloc(io, "common/fixtures/cli/outcome.json", a, .limited(1 << 20));
    const doc = (try std.json.parseFromSliceLeaky(std.json.Value, a, text, .{})).object;

    // The CLI's own success shape carries the page label after " px · "; older shapes are the parsers' to tolerate.
    var own: usize = 0;
    for (doc.get("wrote").?.array.items) |c| {
        const o = c.object;
        const exp = o.get("expected").?;
        if (exp == .null) continue;
        const stderr = o.get("stderr").?.string;
        const at = std.mem.indexOf(u8, stderr, " px · ") orelse continue;
        const page = stderr[at + " px · ".len .. std.mem.lastIndexOfScalar(u8, stderr, ')').?];
        const e = exp.object;
        const want = try render(a, wrote.wrote, .{ e.get("path").?.string, int(e.get("width").?), int(e.get("height").?), page });
        try testing.expect(hasLine(stderr, want));
        own += 1;
    }
    try testing.expect(own >= 2);

    var lines: usize = 0;
    for (doc.get("remotes").?.array.items) |c| {
        const stderr = c.object.get("stderr").?.string;
        for (c.object.get("expected").?.array.items) |r| {
            const e = r.object;
            const action = e.get("action").?.string;
            const want = if (std.mem.eql(u8, action, "updated"))
                try render(a, remote.updated_result, .{ e.get("id").?.string, int(e.get("width").?), int(e.get("height").?) })
            else
                try render(a, remote.created_project, .{ e.get("name").?.string, e.get("id").?.string });
            try testing.expect(hasLine(stderr, want));
            lines += 1;
        }
    }
    try testing.expect(lines >= 3);

    // A case with no `error: ` line in its stderr pins the parsers' fallback, not a CLI line.
    for (doc.get("errors").?.array.items) |c| {
        const exp = c.object.get("expected").?;
        if (exp != .string or std.mem.indexOf(u8, c.object.get("stderr").?.string, "error: ") == null) continue;
        var it = std.mem.splitScalar(u8, exp.string, '\n');
        while (it.next()) |l| try testing.expect(std.mem.startsWith(u8, l, "error: "));
    }
}

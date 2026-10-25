//! The scrape stderr corpus (common/fixtures/cli/scrape.json) mcp and bot parse, reproduced from
//! the CLI's side: every line a case holds is the one run.zig's `line` formats print for its
//! files, host, count and error.
const std = @import("std");
const line = @import("../../src/scrape/run.zig").line;
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

test "every scrape fixture line is one the CLI prints" {
    var arena_state = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena_state.deinit();
    const a = arena_state.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var root = try openRepoRoot(io);
    defer root.close(io);
    const text = try root.readFileAlloc(io, "common/fixtures/cli/scrape.json", a, .limited(1 << 20));
    const parsed = try std.json.parseFromSliceLeaky(std.json.Value, a, text, .{});
    const cases = parsed.object.get("cases").?.array.items;
    try testing.expect(cases.len >= 4);
    for (cases) |c| {
        const o = c.object;
        var want: std.ArrayList([]const u8) = .empty;
        const stderr = o.get("stderr").?.string;
        if (std.mem.startsWith(u8, stderr, "scraping ")) {
            const url = stderr["scraping ".len..std.mem.indexOf(u8, stderr, "…").?];
            try want.append(a, try render(a, line.start, .{url}));
        }
        if (o.get("error").? == .string) {
            const site = o.get("error").?.string["no media matched at ".len..];
            try want.append(a, try std.fmt.allocPrint(a, "error: {s}", .{try render(a, line.no_media, .{site})}));
        } else {
            const host = o.get("host").?.string;
            for (o.get("files").?.array.items) |f| {
                const fo = f.object;
                const path = fo.get("path").?.string;
                try want.append(a, if (fo.get("width").? == .integer)
                    try render(a, line.wrote_sized, .{ path, fo.get("width").?.integer, fo.get("height").?.integer, host })
                else
                    try render(a, line.wrote, .{ path, host }));
            }
            try want.append(a, try render(a, line.summary, .{ o.get("count").?.integer, host, o.get("dir").?.string }));
        }
        const got = try std.mem.join(a, "\n", want.items);
        testing.expectEqualStrings(stderr, got) catch |e| {
            std.debug.print("scrape fixture '{s}' is not what the CLI prints\n", .{o.get("name").?.string});
            return e;
        };
    }
}

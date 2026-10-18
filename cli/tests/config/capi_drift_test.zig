//! Drift guard for the hand-written C ABI sheet: every `stencil_cli_*` prototype in
//! core/cliApi.h has an `extern fn` of the same name and arity in src/core/cApi.zig, and the
//! sheet declares nothing the header does not.
const std = @import("std");
const cApi = @import("../../src/core/cApi.zig");
const testing = std.testing;

const prefix = "stencil_cli_";

/// The repo root, found by walking up from cwd (cli/ under `zig build test`) to CLAUDE.md.
fn openRepoRoot(io: std.Io) !std.Io.Dir {
    for ([_][]const u8{ ".", "..", "../..", "../../.." }) |rel| {
        var dir = std.Io.Dir.cwd().openDir(io, rel, .{}) catch continue;
        if (dir.access(io, "CLAUDE.md", .{})) |_| return dir else |_| dir.close(io);
    }
    return error.RepoRootNotFound;
}

/// Parameters of the prototype whose `(` is at `open`: `(void)` is 0, else top-level commas + 1.
fn arityAt(header: []const u8, open: usize) !usize {
    var depth: usize = 0;
    var commas: usize = 0;
    var i = open;
    while (i < header.len) : (i += 1) {
        switch (header[i]) {
            '(' => depth += 1,
            ')' => {
                depth -= 1;
                if (depth == 0) break;
            },
            ',' => if (depth == 1) {
                commas += 1;
            },
            else => {},
        }
    }
    if (i == header.len) return error.UnterminatedPrototype;
    const inner = std.mem.trim(u8, header[open + 1 .. i], " \t\r\n");
    if (inner.len == 0 or std.mem.eql(u8, inner, "void")) return 0;
    return commas + 1;
}

test "cApi.zig declares exactly the header's prototypes, with the same arity" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var root = try openRepoRoot(io);
    defer root.close(io);
    const header = try root.readFileAlloc(io, "core/cliApi.h", a, .limited(1 << 20));
    defer a.free(header);

    var declared = std.StringHashMap(usize).init(a);
    defer declared.deinit();
    var at: usize = 0;
    while (std.mem.indexOfPos(u8, header, at, prefix)) |start| {
        var end = start;
        while (end < header.len and (std.ascii.isAlphanumeric(header[end]) or header[end] == '_')) end += 1;
        at = end;
        if (end >= header.len or header[end] != '(') continue;
        try declared.put(header[start..end], try arityAt(header, end));
    }
    try testing.expect(declared.count() >= 60);

    var matched: usize = 0;
    inline for (@typeInfo(cApi).@"struct".decl_names) |name| {
        const arity = @typeInfo(@TypeOf(@field(cApi, name))).@"fn".param_types.len;
        const want = declared.get(name) orelse {
            std.debug.print("cApi.zig declares '{s}', which core/cliApi.h does not\n", .{name});
            return error.TestUnexpectedResult;
        };
        try testing.expectEqual(want, arity);
        matched += 1;
    }
    try testing.expectEqual(declared.count(), matched);
}

//! What the executor needs after validation: the crop spec string a plan's edges become,
//! resolving a named server against the ones this session already connected to (§10), and
//! the console context suffix the assistant is told about.
const std = @import("std");
const host = @import("../../host.zig");
const testing = std.testing;
const model = @import("model.zig");
const CropEdges = model.CropEdges;
const max_label_chars = model.max_label_chars;

/// Join the crop edges back into the CLI's crop-spec grammar (`x1=10% x2=-10%`), the same
/// string `/crop` takes. Caller owns the result.
pub fn cropSpecString(gpa: std.mem.Allocator, edges: CropEdges) error{OutOfMemory}![]u8 {
    var out: std.ArrayList(u8) = .empty;
    errdefer out.deinit(gpa);
    inline for (.{ "x1", "x2", "y1", "y2", "aspect" }) |key| {
        if (@field(edges, key)) |token| {
            if (out.items.len != 0) try out.append(gpa, ' ');
            try out.appendSlice(gpa, key);
            try out.append(gpa, '=');
            try out.appendSlice(gpa, token);
        }
    }
    return out.toOwnedSlice(gpa);
}

/// resolveServer's outcome: the matched entry's index, or why nothing matched (the
/// caller's warning — never a plan failure on the console).
pub const ServerMatch = union(enum) { index: usize, none, ambiguous };

/// §10's connect/disconnect stance over the console's own URL lists: exact URL match, else a UNIQUE
/// host (or host:port) match. The model can never introduce an address the user did not /connect.
pub fn resolveServer(urls: []const []const u8, want_raw: []const u8) ServerMatch {
    const want = std.mem.trim(u8, want_raw, " \t");
    if (want.len == 0) return .none;
    for (urls, 0..) |u, i| {
        if (std.mem.eql(u8, u, want)) return .{ .index = i };
    }
    // A bare IPv6 name may be written with or without brackets; the split strips them.
    const want_host = if (want.len >= 2 and want[0] == '[' and want[want.len - 1] == ']') want[1 .. want.len - 1] else want;
    var found: ?usize = null;
    for (urls, 0..) |u, i| {
        const a = host.authorityOf(u) orelse continue;
        if (std.ascii.eqlIgnoreCase(a.raw, want) or std.ascii.eqlIgnoreCase(a.host, want_host)) {
            if (found != null) return .ambiguous;
            found = i;
        }
    }
    return if (found) |i| .{ .index = i } else .none;
}

/// One live connection as the console-context suffix sees it: the URL and (optionally)
/// its project names — NEVER a token; this struct has nowhere to put one.
pub const ConsoleServer = struct {
    url: []const u8,
    active: bool = false, // hosts the active fetched project
    projects: ?[]const []const u8 = null, // null = not fetched/unreachable (line omitted)
};

/// How many project names one server contributes to the context suffix.
pub const max_context_projects = 20;

/// The console's dynamic system-prompt suffix (§4 allows one): the connection list (URLs only), the
/// active project and each server's project names, so connect ops resolve against the user's own.
pub fn consoleContextAlloc(
    gpa: std.mem.Allocator,
    servers: []const ConsoleServer,
    active_project: []const u8,
) error{OutOfMemory}![]u8 {
    var out: std.ArrayList(u8) = .empty;
    errdefer out.deinit(gpa);
    const w = "Console state (the console's own connections and project, for answering questions about it):\n";
    try out.appendSlice(gpa, w);
    if (servers.len == 0) {
        try out.appendSlice(gpa, "Connections: none — the user can add one with '/connect <url>'.");
    } else {
        try appendFmt(gpa, &out, "Connections ({d}): ", .{servers.len});
        for (servers, 0..) |s, i| {
            if (i != 0) try out.appendSlice(gpa, ", ");
            try out.appendSlice(gpa, s.url);
            if (s.active) try out.appendSlice(gpa, " (active project's server)");
        }
        try out.appendSlice(gpa, ".");
    }
    if (active_project.len != 0) {
        try appendFmt(gpa, &out, "\nActive server project: \"{s}\".", .{active_project});
    } else {
        try out.appendSlice(gpa, "\nActive server project: none.");
    }
    for (servers) |s| {
        const names = s.projects orelse continue; // unknown (unreachable) ≠ empty
        try appendFmt(gpa, &out, "\nProjects on {s}: ", .{s.url});
        if (names.len == 0) {
            try out.appendSlice(gpa, "(none)");
            continue;
        }
        const shown = @min(names.len, max_context_projects);
        for (names[0..shown], 0..) |n, i| {
            if (i != 0) try out.appendSlice(gpa, ", ");
            try out.appendSlice(gpa, n);
        }
        if (names.len > shown) try appendFmt(gpa, &out, " (+{d} more)", .{names.len - shown});
        try out.appendSlice(gpa, ".");
    }
    return out.toOwnedSlice(gpa);
}

fn appendFmt(gpa: std.mem.Allocator, out: *std.ArrayList(u8), comptime fmt: []const u8, fargs: anytype) error{OutOfMemory}!void {
    const s = try std.fmt.allocPrint(gpa, fmt, fargs);
    defer gpa.free(s);
    try out.appendSlice(gpa, s);
}

/// Sanitize a variant label into a `[a-z0-9-]` file stem (runs of other characters become single
/// dashes, capped at 40, dangling dashes trimmed). May come out empty; caller owns the result.
pub fn sanitizeLabel(gpa: std.mem.Allocator, label: []const u8) error{OutOfMemory}![]u8 {
    var out: std.ArrayList(u8) = .empty;
    errdefer out.deinit(gpa);
    for (label) |raw| {
        const c = std.ascii.toLower(raw);
        if (std.ascii.isLower(c) or std.ascii.isDigit(c)) {
            if (out.items.len >= max_label_chars) break;
            try out.append(gpa, c);
        } else if (out.items.len != 0 and out.items[out.items.len - 1] != '-') {
            try out.append(gpa, '-');
        }
    }
    while (out.items.len != 0 and out.items[out.items.len - 1] == '-') _ = out.pop();
    return out.toOwnedSlice(gpa);
}

test "sanitizeLabel + cropSpecString" {
    const a = testing.allocator;

    const s1 = try sanitizeLabel(a, "Rotated & Tinted (v2)");
    defer a.free(s1);
    try testing.expectEqualStrings("rotated-tinted-v2", s1);
    const s2 = try sanitizeLabel(a, "___");
    defer a.free(s2);
    try testing.expectEqualStrings("", s2); // callers fall back to the position
    const s3 = try sanitizeLabel(a, "a" ** 60);
    defer a.free(s3);
    try testing.expectEqual(@as(usize, max_label_chars), s3.len);

    const spec = try cropSpecString(a, .{ .x1 = "10%", .y2 = "-2cm" });
    defer a.free(spec);
    try testing.expectEqualStrings("x1=10% y2=-2cm", spec);

    // The aspect ratio rides along as its own `k=v` token, resolved core-side.
    const with_aspect = try cropSpecString(a, .{ .x1 = "10%", .aspect = "4:3" });
    defer a.free(with_aspect);
    try testing.expectEqualStrings("x1=10% aspect=4:3", with_aspect);
}

test "resolveServer: exact URL, else a UNIQUE host — never a model-introduced address (§10)" {
    const urls = [_][]const u8{ "http://alpha.example:8090", "https://beta.example", "http://alpha.example:9091" };

    // Exact URL match wins outright, even when the host is ambiguous.
    try testing.expectEqual(@as(usize, 0), resolveServer(&urls, "http://alpha.example:8090").index);
    // host:port and bare-host matches, case-insensitive, whitespace-trimmed.
    try testing.expectEqual(@as(usize, 2), resolveServer(&urls, "alpha.example:9091").index);
    try testing.expectEqual(@as(usize, 1), resolveServer(&urls, "BETA.example").index);
    try testing.expectEqual(@as(usize, 1), resolveServer(&urls, "  beta.example  ").index);
    // A bare host two entries share is ambiguous, not a guess.
    try testing.expect(resolveServer(&urls, "alpha.example") == .ambiguous);
    // Anything else — including an empty name — resolves to nothing.
    try testing.expect(resolveServer(&urls, "gamma.example") == .none);
    try testing.expect(resolveServer(&urls, "http://alpha.example:1234") == .none);
    try testing.expect(resolveServer(&urls, "") == .none);
    try testing.expect(resolveServer(&.{}, "alpha.example") == .none);
}

test "consoleContextAlloc: connections + active project + capped project names, tokens impossible" {
    const a = testing.allocator;

    // No connections: the context still stands, pointing at the console's own command.
    const empty = try consoleContextAlloc(a, &.{}, "");
    defer a.free(empty);
    try testing.expect(std.mem.indexOf(u8, empty, "Connections: none") != null);
    try testing.expect(std.mem.indexOf(u8, empty, "'/connect <url>'") != null);
    try testing.expect(std.mem.indexOf(u8, empty, "Active server project: none.") != null);

    // 22 project names on the active server: capped at 20 with a "+2 more".
    var many: [22][]const u8 = undefined;
    var bufs: [22][8]u8 = undefined;
    for (0..22) |i| many[i] = std.fmt.bufPrint(&bufs[i], "p{d}", .{i}) catch unreachable;
    const servers = [_]ConsoleServer{
        .{ .url = "http://a:8090", .active = true, .projects = &many },
        .{ .url = "http://b:9091", .projects = &.{} },
        .{ .url = "http://c:9092" }, // projects unknown (unreachable) → no listing line
    };
    const ctx = try consoleContextAlloc(a, &servers, "portrait");
    defer a.free(ctx);
    try testing.expect(std.mem.indexOf(u8, ctx, "Connections (3): http://a:8090 (active project's server), http://b:9091, http://c:9092.") != null);
    try testing.expect(std.mem.indexOf(u8, ctx, "Active server project: \"portrait\".") != null);
    try testing.expect(std.mem.indexOf(u8, ctx, "p19 (+2 more).") != null);
    try testing.expect(std.mem.indexOf(u8, ctx, "p20") == null); // beyond the cap
    try testing.expect(std.mem.indexOf(u8, ctx, "Projects on http://b:9091: (none)") != null);
    try testing.expect(std.mem.indexOf(u8, ctx, "Projects on http://c:9092") == null);
    // The suffix is built from URLs + names alone — ConsoleServer has no token field,
    // so nothing token-shaped can ever reach the prompt from here.
    try testing.expect(!@hasField(ConsoleServer, "token"));
}

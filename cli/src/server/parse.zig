//! Reading the server's JSON answers: one project's id/version/metadata, the project
//! list, and the error body behind a rejected request. Every returned string is owned.
const std = @import("std");
const Error = @import("errors.zig").Error;
const testing = std.testing;

/// Parse a { "token": "..." } response, returning an owned copy of the token.
pub fn parseToken(gpa: std.mem.Allocator, body: []const u8) ![]u8 {
    const T = struct { token: []const u8 };
    var p = std.json.parseFromSlice(T, gpa, body, .{ .ignore_unknown_fields = true }) catch return Error.BadResponse;
    defer p.deinit();
    return gpa.dupe(u8, p.value.token);
}

/// Parse a created/returned project record { "id": "..." }, returning the owned id.
pub fn parseProjectId(gpa: std.mem.Allocator, body: []const u8) ![]u8 {
    const T = struct { id: []const u8 };
    var p = std.json.parseFromSlice(T, gpa, body, .{ .ignore_unknown_fields = true }) catch return Error.BadResponse;
    defer p.deinit();
    return gpa.dupe(u8, p.value.id);
}

/// Find a project id by (case-insensitive) name in a { "projects": [...] } list body.
pub fn findIdByName(gpa: std.mem.Allocator, body: []const u8, name: []const u8) !?[]u8 {
    const T = struct {
        projects: []const struct { id: []const u8, name: []const u8 },
    };
    var p = std.json.parseFromSlice(T, gpa, body, .{ .ignore_unknown_fields = true }) catch return Error.BadResponse;
    defer p.deinit();
    for (p.value.projects) |proj| {
        if (std.ascii.eqlIgnoreCase(proj.name, name)) return try gpa.dupe(u8, proj.id);
    }
    return null;
}

/// A project reference resolved from a list: its id plus the current server version, which seeds the
/// console's last-writer-wins guard so it knows which incoming events are newer. Caller owns `id`.
pub const ProjectRef = struct { id: []u8, version: i64 };

/// Like findIdByName, but also captures the project's monotonic edit version.
pub fn findProjectByName(gpa: std.mem.Allocator, body: []const u8, name: []const u8) !?ProjectRef {
    const T = struct {
        projects: []const struct { id: []const u8, name: []const u8, version: i64 = 0 },
    };
    var p = std.json.parseFromSlice(T, gpa, body, .{ .ignore_unknown_fields = true }) catch return Error.BadResponse;
    defer p.deinit();
    for (p.value.projects) |proj| {
        if (std.ascii.eqlIgnoreCase(proj.name, name))
            return ProjectRef{ .id = try gpa.dupe(u8, proj.id), .version = proj.version };
    }
    return null;
}

/// Parse a single-project body ({ "project": { ..., "version": N } }) for its version.
pub fn parseProjectVersion(gpa: std.mem.Allocator, body: []const u8) !i64 {
    const T = struct { project: struct { version: i64 = 0 } };
    var p = std.json.parseFromSlice(T, gpa, body, .{ .ignore_unknown_fields = true }) catch return Error.BadResponse;
    defer p.deinit();
    return p.value.project.version;
}

/// One project as shown by `/projects`: name + image size + last-change timestamp, plus its custom name
/// colour ("" = paint in the theme accent) and free-text description ("" = none). Owns all three.
pub const ProjectInfo = struct { name: []u8, created_at: i64, updated_at: i64, expires_at: i64, w: i64, h: i64, color: []u8, description: []u8, keywords: [][]u8 };

/// Free a slice of owned strings (each string, then the slice). Used for keyword lists.
pub fn freeStrList(gpa: std.mem.Allocator, items: [][]u8) void {
    for (items) |s| gpa.free(s);
    gpa.free(items);
}

/// Dupe a slice of borrowed strings into an owned [][]u8 (free with freeStrList).
pub fn dupeStrList(gpa: std.mem.Allocator, src: []const []const u8) ![][]u8 {
    var list: std.ArrayList([]u8) = .empty;
    errdefer freeStrList(gpa, list.toOwnedSlice(gpa) catch &.{});
    for (src) |s| try list.append(gpa, try gpa.dupe(u8, s));
    return list.toOwnedSlice(gpa);
}

/// Parse a { "projects": [...] } list body into an owned slice of ProjectInfo. Free with
/// freeProjectList. Pure — unit-tested without a socket.
pub fn parseProjectList(gpa: std.mem.Allocator, body: []const u8) ![]ProjectInfo {
    const T = struct {
        projects: []const struct {
            name: []const u8 = "",
            createdAt: i64 = 0,
            updatedAt: i64 = 0,
            expiresAt: i64 = 0,
            imageW: i64 = 0,
            imageH: i64 = 0,
            color: []const u8 = "",
            description: []const u8 = "",
            keywords: []const []const u8 = &.{},
        },
    };
    var p = std.json.parseFromSlice(T, gpa, body, .{ .ignore_unknown_fields = true }) catch return Error.BadResponse;
    defer p.deinit();
    var list: std.ArrayList(ProjectInfo) = .empty;
    errdefer freeProjectList(gpa, list.toOwnedSlice(gpa) catch &.{});
    for (p.value.projects) |proj| {
        const nm = try gpa.dupe(u8, proj.name);
        errdefer gpa.free(nm);
        const col = try gpa.dupe(u8, proj.color);
        errdefer gpa.free(col);
        const desc = try gpa.dupe(u8, proj.description);
        errdefer gpa.free(desc);
        const kws = try dupeStrList(gpa, proj.keywords);
        errdefer freeStrList(gpa, kws);
        try list.append(gpa, .{ .name = nm, .created_at = proj.createdAt, .updated_at = proj.updatedAt, .expires_at = proj.expiresAt, .w = proj.imageW, .h = proj.imageH, .color = col, .description = desc, .keywords = kws });
    }
    return list.toOwnedSlice(gpa);
}

/// Free a slice returned by parseProjectList (each owned name + colour + description + keywords,
/// then the slice).
pub fn freeProjectList(gpa: std.mem.Allocator, items: []ProjectInfo) void {
    for (items) |it| {
        gpa.free(it.name);
        gpa.free(it.color);
        gpa.free(it.description);
        freeStrList(gpa, it.keywords);
    }
    gpa.free(items);
}

/// Parse a single-project body ({ "project": { ..., "keywords": [...] } }) into an owned
/// [][]u8 (free with freeStrList). "" / absent → empty. Pure — unit-tested without a socket.
pub fn parseProjectKeywords(gpa: std.mem.Allocator, body: []const u8) ![][]u8 {
    const T = struct { project: struct { keywords: []const []const u8 = &.{} } };
    var p = std.json.parseFromSlice(T, gpa, body, .{ .ignore_unknown_fields = true }) catch return Error.BadResponse;
    defer p.deinit();
    return dupeStrList(gpa, p.value.project.keywords);
}

/// Parse one string field out of a single-project body — "color", "blankColor" ("" = not a blank
/// project), "description". "" when absent, empty or non-string. Caller owns the returned slice.
pub fn parseProjectStringField(gpa: std.mem.Allocator, body: []const u8, key: []const u8) ![]u8 {
    var p = std.json.parseFromSlice(std.json.Value, gpa, body, .{}) catch return Error.BadResponse;
    defer p.deinit();
    if (p.value != .object) return Error.BadResponse;
    const proj = p.value.object.get("project") orelse return Error.BadResponse;
    if (proj != .object) return Error.BadResponse;
    const v = proj.object.get(key) orelse return gpa.dupe(u8, "");
    return gpa.dupe(u8, if (v == .string) v.string else "");
}

/// Parse the server's { "code", "message" } error body for its message; null when the
/// body isn't that shape (callers then show the raw body). Caller owns the slice.
pub fn parseErrorMessage(gpa: std.mem.Allocator, body: []const u8) ?[]u8 {
    const T = struct { message: []const u8 };
    var p = std.json.parseFromSlice(T, gpa, body, .{ .ignore_unknown_fields = true }) catch return null;
    defer p.deinit();
    return gpa.dupe(u8, p.value.message) catch null;
}

test "parseToken / parseProjectId" {
    const a = testing.allocator;
    const tok = try parseToken(a, "{\"token\":\"abc123\",\"expiresAt\":0}");
    defer a.free(tok);
    try testing.expectEqualStrings("abc123", tok);

    const id = try parseProjectId(a, "{\"id\":\"p_x_y\",\"name\":\"N\",\"version\":0}");
    defer a.free(id);
    try testing.expectEqualStrings("p_x_y", id);
}

test "parseErrorMessage reads the server's error body, null for other shapes" {
    const a = testing.allocator;
    const msg = parseErrorMessage(a, "{\"code\":\"unauthorized\",\"message\":\"admin token required to issue tokens\"}").?;
    defer a.free(msg);
    try testing.expectEqualStrings("admin token required to issue tokens", msg);

    try testing.expect(parseErrorMessage(a, "not json") == null);
    try testing.expect(parseErrorMessage(a, "{\"code\":\"x\"}") == null); // no message field
}

test "findProjectByName captures id + version; parseProjectVersion reads a single project" {
    const a = testing.allocator;
    const list =
        "{\"projects\":[{\"id\":\"p_1_a\",\"name\":\"Alpha\",\"version\":4},{\"id\":\"p_2_b\",\"name\":\"Beta\",\"version\":9}]}";
    const ref = (try findProjectByName(a, list, "beta")).?;
    defer a.free(ref.id);
    try testing.expectEqualStrings("p_2_b", ref.id);
    try testing.expectEqual(@as(i64, 9), ref.version);
    try testing.expect((try findProjectByName(a, list, "missing")) == null);

    const v = try parseProjectVersion(a, "{\"project\":{\"id\":\"p_2_b\",\"name\":\"Beta\",\"version\":9}}");
    try testing.expectEqual(@as(i64, 9), v);
}

test "parseProjectList yields owned name/size/updatedAt/color records" {
    const a = testing.allocator;
    const body =
        "{\"projects\":[{\"id\":\"p1\",\"name\":\"Alpha\",\"imageW\":800,\"imageH\":600,\"updatedAt\":1700000000000,\"expiresAt\":1700009999000,\"color\":\"#ff5623\",\"description\":\"lead shot\"}," ++
        "{\"id\":\"p2\",\"name\":\"Beta\",\"imageW\":1024,\"imageH\":768,\"updatedAt\":0}]}";
    const items = try parseProjectList(a, body);
    defer freeProjectList(a, items);
    try testing.expectEqual(@as(usize, 2), items.len);
    try testing.expectEqualStrings("Alpha", items[0].name);
    try testing.expectEqual(@as(i64, 800), items[0].w);
    try testing.expectEqual(@as(i64, 600), items[0].h);
    try testing.expectEqual(@as(i64, 1700000000000), items[0].updated_at);
    try testing.expectEqual(@as(i64, 1700009999000), items[0].expires_at);
    try testing.expectEqualStrings("#ff5623", items[0].color);
    try testing.expectEqualStrings("lead shot", items[0].description);
    try testing.expectEqualStrings("Beta", items[1].name);
    try testing.expectEqual(@as(i64, 0), items[1].expires_at); // no expiry → 0 (never)
    try testing.expectEqualStrings("", items[1].color); // no custom colour → empty
    try testing.expectEqualStrings("", items[1].description); // no description → empty

    // An empty list parses to an empty (non-null) slice.
    const none = try parseProjectList(a, "{\"projects\":[]}");
    defer freeProjectList(a, none);
    try testing.expectEqual(@as(usize, 0), none.len);
}

test "parseProjectStringField reads one project field, empty when absent" {
    const a = testing.allocator;
    const body = "{\"project\":{\"id\":\"p_1\",\"name\":\"N\",\"color\":\"#7c3aed\",\"blankColor\":\"#fff\",\"description\":\"a caption\"}}";
    for ([_][2][]const u8{
        .{ "color", "#7c3aed" },
        .{ "blankColor", "#fff" },
        .{ "description", "a caption" },
    }) |c| {
        const got = try parseProjectStringField(a, body, c[0]);
        defer a.free(got);
        try testing.expectEqualStrings(c[1], got);
    }

    const none = try parseProjectStringField(a, "{\"project\":{\"id\":\"p_1\",\"name\":\"N\"}}", "color");
    defer a.free(none);
    try testing.expectEqualStrings("", none);

    try testing.expectError(Error.BadResponse, parseProjectStringField(a, "{\"nope\":1}", "color"));
}

test "findIdByName matches case-insensitively, else null" {
    const a = testing.allocator;
    const body =
        "{\"projects\":[{\"id\":\"p_1_a\",\"name\":\"Alpha\"},{\"id\":\"p_2_b\",\"name\":\"Beta\"}]}";
    const id = (try findIdByName(a, body, "beta")).?;
    defer a.free(id);
    try testing.expectEqualStrings("p_2_b", id);

    try testing.expect((try findIdByName(a, body, "missing")) == null);
}

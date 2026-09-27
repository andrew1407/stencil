//! Reading the server's JSON answers: one project's id/version/metadata, the project
//! list and its pages, and the error body behind a rejected request. Every returned string is owned.
const std = @import("std");
const Error = @import("errors.zig").Error;

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

/// Whether a `/projects` page names a next one (a non-empty string `nextCursor`).
pub fn hasNextCursor(gpa: std.mem.Allocator, body: []const u8) bool {
    var p = std.json.parseFromSlice(std.json.Value, gpa, body, .{}) catch return false;
    defer p.deinit();
    return nextCursor(p.value) != null;
}

fn nextCursor(page: std.json.Value) ?[]const u8 {
    const next = (if (page == .object) page.object.get("nextCursor") else null) orelse return null;
    return if (next == .string and next.string.len > 0) next.string else null;
}

/// `GET /projects` walked page by page into one `{"projects":[…]}` body. A cursor handed back twice,
/// or a walk past max_pages, is an error rather than a loop.
pub const ProjectPages = struct {
    pub const max_pages = 1000;
    gpa: std.mem.Allocator,
    out: std.Io.Writer.Allocating,
    seen: std.StringHashMapUnmanaged(void) = .empty, // owned cursors already followed
    pages: usize = 0,
    items: usize = 0,

    pub fn deinit(self: *ProjectPages) void {
        self.out.deinit();
        var it = self.seen.keyIterator();
        while (it.next()) |k| self.gpa.free(k.*);
        self.seen.deinit(self.gpa);
    }

    /// Append one page's projects; the next page's path (owned), or null when this page is the last.
    pub fn add(self: *ProjectPages, body: []const u8) !?[]u8 {
        var p = std.json.parseFromSlice(std.json.Value, self.gpa, body, .{}) catch return Error.BadResponse;
        defer p.deinit();
        if (p.value != .object) return Error.BadResponse;
        const list = p.value.object.get("projects") orelse return Error.BadResponse;
        if (list != .array) return Error.BadResponse;
        const w = &self.out.writer;
        if (self.pages == 0) try w.writeAll("{\"projects\":[");
        for (list.array.items) |item| {
            if (self.items > 0) try w.writeByte(',');
            try std.json.Stringify.value(item, .{}, w);
            self.items += 1;
        }
        self.pages += 1;
        const cursor = nextCursor(p.value) orelse return null;
        if (self.pages == max_pages) return error.TooManyPages;
        if (self.seen.contains(cursor)) return error.CursorLoop;
        try self.seen.ensureUnusedCapacity(self.gpa, 1);
        const owned = try self.gpa.dupe(u8, cursor);
        self.seen.putAssumeCapacityNoClobber(owned, {});
        return try pagePath(self.gpa, owned);
    }

    /// The merged body; owned by the caller.
    pub fn finish(self: *ProjectPages) ![]u8 {
        try self.out.writer.writeAll("]}");
        return self.out.toOwnedSlice();
    }
};

/// `/projects?after=` plus the cursor percent-encoded byte by byte, unreserved kept.
fn pagePath(gpa: std.mem.Allocator, cursor: []const u8) ![]u8 {
    var path: std.Io.Writer.Allocating = .init(gpa);
    errdefer path.deinit();
    try path.writer.writeAll("/projects?after=");
    for (cursor) |b| {
        if (std.ascii.isAlphanumeric(b) or std.mem.indexOfScalar(u8, "-_.~", b) != null) {
            try path.writer.writeByte(b);
        } else try path.writer.print("%{X:0>2}", .{b});
    }
    return path.toOwnedSlice();
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

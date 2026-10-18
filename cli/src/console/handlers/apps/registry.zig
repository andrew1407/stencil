//! Which `.stencil` files the desktop app has open: its registry (`projects.json`, one row per
//! project, a `linkedFile` on the ones synced to a file) and `projectOpened.json` (the ids open
//! now) under its state directory. Pure over bytes but for `load`, which reads the first dir found.
const std = @import("std");
const AppEnv = @import("env.zig").AppEnv;

pub const LinkedRow = struct {
    id: []const u8,
    name: []const u8,
    path: []const u8,
    open: bool,
    updated_at: i64,
};

/// Where the desktop keeps its state, most specific first: `STENCIL_STATE_DIR`, a dev build's
/// `desktop/.stencil` beside this checkout, then Qt's AppConfigLocation for the app "Stencil".
pub fn stateDirs(gpa: std.mem.Allocator, env: AppEnv) ![][]u8 {
    var out: std.ArrayList([]u8) = .empty;
    errdefer {
        for (out.items) |d| gpa.free(d);
        out.deinit(gpa);
    }
    if (env.state_dir) |d| try out.append(gpa, try gpa.dupe(u8, d));
    for ([_][]const u8{ "desktop/.stencil", "../desktop/.stencil", "../../desktop/.stencil" }) |rel| try out.append(gpa, try gpa.dupe(u8, rel));
    if (env.home) |home| {
        const base: []const u8 = switch (@import("builtin").os.tag) {
            .macos => "Library/Preferences",
            .windows => "AppData/Local",
            else => ".config",
        };
        try out.append(gpa, try gpa.print("{s}/{s}/Stencil", .{ home, base }));
        try out.append(gpa, try gpa.print("{s}/{s}/Stencil/Stencil", .{ home, base }));
    }
    return out.toOwnedSlice(gpa);
}

/// The rows of `projects_json` that carry a `linkedFile`, the open ones (`opened_json`) first,
/// then the most recently updated. Owned by `arena`; malformed input yields no rows.
pub fn parseLinked(arena: std.mem.Allocator, projects_json: []const u8, opened_json: []const u8) ![]LinkedRow {
    const projects = std.json.parseFromSliceLeaky(std.json.Value, arena, projects_json, .{}) catch return &.{};
    if (projects != .array) return &.{};
    const opened = std.json.parseFromSliceLeaky(std.json.Value, arena, opened_json, .{}) catch std.json.Value.null;
    var rows: std.ArrayList(LinkedRow) = .empty;
    for (projects.array.items) |row| {
        if (row != .object) continue;
        const path = str(row.object, "linkedFile") orelse continue;
        if (path.len == 0) continue;
        const id = str(row.object, "id") orelse "";
        try rows.append(arena, .{
            .id = id,
            .name = str(row.object, "name") orelse "",
            .path = path,
            .open = opened == .object and opened.object.contains(id),
            .updated_at = if (row.object.get("updatedAt")) |u| (if (u == .integer) u.integer else 0) else 0,
        });
    }
    std.mem.sort(LinkedRow, rows.items, {}, byOpenThenRecent);
    return rows.toOwnedSlice(arena);
}

fn byOpenThenRecent(_: void, a: LinkedRow, b: LinkedRow) bool {
    if (a.open != b.open) return a.open;
    return a.updated_at > b.updated_at;
}

fn str(obj: std.json.ObjectMap, key: []const u8) ?[]const u8 {
    const v = obj.get(key) orelse return null;
    return if (v == .string) v.string else null;
}

/// The linked rows of the first state directory holding a registry; none when there is none.
pub fn load(arena: std.mem.Allocator, io: std.Io, env: AppEnv) ![]LinkedRow {
    const dirs = try stateDirs(arena, env);
    for (dirs) |dir| {
        const projects_path = try std.fs.path.join(arena, &.{ dir, "projects.json" });
        const projects = std.Io.Dir.cwd().readFileAlloc(io, projects_path, arena, .limited(16 << 20)) catch continue;
        const opened_path = try std.fs.path.join(arena, &.{ dir, "projectOpened.json" });
        const opened = std.Io.Dir.cwd().readFileAlloc(io, opened_path, arena, .limited(1 << 20)) catch "";
        return parseLinked(arena, projects, opened);
    }
    return &.{};
}

const testing = std.testing;

test "parseLinked keeps the rows with a linked file, open ones first, then the most recent" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    const projects =
        \\[{"id":"p1","name":"Old","updatedAt":10,"linkedFile":"/a/old.stencil"},
        \\ {"id":"p2","name":"Plain","updatedAt":50},
        \\ {"id":"p3","name":"New","updatedAt":30,"linkedFile":"/a/new.stencil"},
        \\ {"id":"p4","name":"Open","updatedAt":5,"linkedFile":"/a/open.stencil"}, 7, {"id":"p5","linkedFile":""}]
    ;
    const rows = try parseLinked(a, projects, "{\"p4\": 1791667494851}");
    try testing.expectEqual(@as(usize, 3), rows.len);
    try testing.expectEqualStrings("Open", rows[0].name);
    try testing.expect(rows[0].open);
    try testing.expectEqualStrings("New", rows[1].name);
    try testing.expectEqualStrings("/a/old.stencil", rows[2].path);
    try testing.expect(!rows[2].open);
    try testing.expectEqual(@as(usize, 0), (try parseLinked(a, "not json", "")).len);
    try testing.expectEqual(@as(usize, 0), (try parseLinked(a, "{}", "")).len);
}

test "stateDirs: the environment's directory first, the dev build's, then the app's own" {
    const a = testing.allocator;
    const dirs = try stateDirs(a, .{ .state_dir = "/tmp/st", .home = "/home/me" });
    defer {
        for (dirs) |d| a.free(d);
        a.free(dirs);
    }
    try testing.expectEqualStrings("/tmp/st", dirs[0]);
    try testing.expectEqualStrings("desktop/.stencil", dirs[1]);
    try testing.expect(std.mem.startsWith(u8, dirs[dirs.len - 1], "/home/me/"));
    try testing.expect(std.mem.endsWith(u8, dirs[dirs.len - 1], "/Stencil/Stencil"));
}

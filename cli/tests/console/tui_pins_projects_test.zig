//! Golden pins for the /projects table: one server's rows with every column's edge case, two
//! servers listed together, and the headers alone, at a fixed "now".
const std = @import("std");
const server = @import("../../src/server/client.zig");
const projectsTable = @import("../../src/console/render/projectsTable.zig");
const pin = @import("tui_pins_harness.zig").pin;
const testing = std.testing;

// Fixed "now" for the relative CREATED / CHANGED / EXPIRES columns.
const now_ms: i64 = 1_700_000_000_000;
const minute: i64 = 60 * 1000;
const day: i64 = 24 * 60 * minute;

/// Append one row exactly as gatherRows builds it (same size fallback, same relative
/// formatting), so the pin covers what a real listing renders.
fn addRow(
    a: std.mem.Allocator,
    rows: *std.ArrayList(projectsTable.ProjectRow),
    name: []const u8,
    w: i64,
    h: i64,
    created_ms: i64,
    updated_ms: i64,
    expires_ms: i64,
    color: []const u8,
    description: []const u8,
    srv: []const u8,
) !void {
    var tb: [32]u8 = undefined;
    const size = if (w == 0 and h == 0) try a.dupe(u8, "-") else try std.fmt.allocPrint(a, "{d}x{d}", .{ w, h });
    const created = try a.dupe(u8, server.formatAgo(&tb, now_ms, created_ms));
    const expires = try a.dupe(u8, server.formatUntil(&tb, now_ms, expires_ms));
    const changed = try a.dupe(u8, server.formatAgo(&tb, now_ms, updated_ms));
    try rows.append(a, .{
        .name = try a.dupe(u8, name),
        .size = size,
        .created = created,
        .expires = expires,
        .changed = changed,
        .color = try a.dupe(u8, color),
        .description = try a.dupe(u8, description),
        .server = srv,
    });
}

// One local server's projects: a plain row, a name long enough to widen the NAME column beside an
// ellipsized description, an expiring one, an expired one, and a coloured project with no stored size.
fn renderProjects(a: std.mem.Allocator) anyerror!void {
    var rows: std.ArrayList(projectsTable.ProjectRow) = .empty;
    defer projectsTable.freeRows(a, &rows);
    try addRow(a, &rows, "poster", 1200, 800, now_ms - 3 * day, now_ms - 5 * minute, 0, "", "", "");
    try addRow(a, &rows, "a-very-long-project-name-that-runs-past-the-column", 640, 480, now_ms - 90 * day, now_ms - 26 * 60 * minute, 0, "", "a description long enough that the trailing note is cut back to a codepoint boundary", "");
    try addRow(a, &rows, "expiring-soon", 800, 600, now_ms - 2 * day, now_ms - 2 * day, now_ms + 3 * day, "", "expires in three days", "");
    try addRow(a, &rows, "stale", 100, 100, now_ms - 400 * day, now_ms - 300 * day, now_ms - day, "", "", "");
    try addRow(a, &rows, "unrendered", 0, 0, now_ms - 30 * 1000, now_ms - 30 * 1000, 0, "#ff8800", "", "");
    projectsTable.renderTable(a, rows.items, false);
}

// Two servers listed together: the SERVER column appears and every row names its origin.
fn renderProjectsMulti(a: std.mem.Allocator) anyerror!void {
    var rows: std.ArrayList(projectsTable.ProjectRow) = .empty;
    defer projectsTable.freeRows(a, &rows);
    try addRow(a, &rows, "poster", 1200, 800, now_ms - 3 * day, now_ms - 5 * minute, 0, "", "", "http://localhost:8080");
    try addRow(a, &rows, "shared-remote", 2048, 1536, now_ms - 10 * day, now_ms - 45 * minute, now_ms + 7 * day, "#4f8cff", "a remote project on the second server", "https://stencil.example.com");
    projectsTable.renderTable(a, rows.items, true);
}

fn renderProjectsEmpty(a: std.mem.Allocator) anyerror!void {
    projectsTable.renderTable(a, &.{}, false); // headers only — no projects on the server
}

test "pins: the /projects table" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    try pin(io, "projects", renderProjects);
    try pin(io, "projects-multi", renderProjectsMulti);
    try pin(io, "projects-empty", renderProjectsEmpty);
}

//! Golden pins for every rendered terminal surface: `--help`, the logo art, and the console listings
//! (intro, /help, /theme, /filter, /format, /projects). Each render is captured through logo.zig's sink
//! seam — the same one the full-screen console installs — and compared byte-for-byte with
//! tests/pins/<name>.<variant>.txt, SGR escapes included: they are user-visible output.
//! `STENCIL_UPDATE_PINS=1 zig build test` rewrites the goldens.
const std = @import("std");
const logo = @import("../src/app/logo.zig");
const theme = @import("../src/app/theme.zig");
const server = @import("../src/server/client.zig");
const ui = @import("../src/console/ui.zig");
const projectsTable = @import("../src/console/render/projectsTable.zig");
const Session = @import("../src/console/session.zig").Session;
const testing = std.testing;

const pins_dir = "tests/pins"; // `zig build test` runs with cwd = cli/
// The terminal width the pins are rendered at. Nothing captured here wraps today, so fixing it
// explicitly makes a renderer that starts wrapping show up as a diff, not a machine-dependent pin.
const pin_cols = 100;

// Collects logo.print output through the sink seam the full-screen console installs.
const Cap = struct {
    buf: std.ArrayList(u8) = .empty,
    fn sink(ctx: *anyopaque, bytes: []const u8) void {
        const self: *Cap = @ptrCast(@alignCast(ctx));
        self.buf.appendSlice(testing.allocator, bytes) catch {};
    }
};

const Render = *const fn (std.mem.Allocator) anyerror!void;

/// Pin one render in both variants: colour forced on (escapes and all) and NO_COLOR.
fn pin(io: std.Io, name: []const u8, body: Render) !void {
    try pinVariant(io, name, "color", false, body);
    try pinVariant(io, name, "plain", true, body);
}

fn pinVariant(io: std.Io, name: []const u8, variant: []const u8, no_color: bool, body: Render) !void {
    var cap = Cap{};
    defer cap.buf.deinit(testing.allocator);
    logo.setSink(Cap.sink, &cap);
    defer logo.clearSink();
    defer logo.init(false, false); // module defaults for whatever runs next

    resetUi(no_color);
    try body(testing.allocator);

    var pbuf: [128]u8 = undefined;
    const path = try std.fmt.bufPrint(&pbuf, "{s}/{s}.{s}.txt", .{ pins_dir, name, variant });
    if (updating()) {
        std.Io.Dir.cwd().createDirPath(io, pins_dir) catch {};
        try std.Io.Dir.cwd().writeFile(io, .{ .sub_path = path, .data = cap.buf.items });
        return;
    }
    try compare(io, path, cap.buf.items);
}

/// Fixed UI state for every pin: default (violet) accent, no accent sentinel, colour driven
/// only by `no_color`, non-interactive.
fn resetUi(no_color: bool) void {
    logo.setAccentSentinel(false);
    logo.setAccent(theme.rgbOf(theme.default_key));
    logo.init(no_color, true); // treat stderr as a terminal, so severity colour is on too
    ui.setInteractive(false);
    ui.setAccent(theme.default_key);
}

fn updating() bool {
    const v = std.c.getenv("STENCIL_UPDATE_PINS") orelse return false;
    const s = std.mem.span(v);
    return s.len != 0 and !std.mem.eql(u8, s, "0");
}

fn compare(io: std.Io, path: []const u8, got: []const u8) !void {
    const want = std.Io.Dir.cwd().readFileAlloc(io, path, testing.allocator, .limited(1 << 20)) catch |e| {
        std.debug.print("pin '{s}' unreadable ({s}) — record it with STENCIL_UPDATE_PINS=1 zig build test\n", .{ path, @errorName(e) });
        return error.MissingPin;
    };
    defer testing.allocator.free(want);
    if (std.mem.eql(u8, want, got)) return;
    try printDiff(path, want, got);
    return error.PinMismatch;
}

/// Print the first differing lines with three lines of context, escapes shown literally
/// (\x1b, \x01) — a raw byte compare says nothing about which escape moved.
fn printDiff(path: []const u8, want: []const u8, got: []const u8) !void {
    const a = testing.allocator;
    const wl = try lines(a, want);
    defer a.free(wl);
    const gl = try lines(a, got);
    defer a.free(gl);

    var first: usize = 0;
    while (first < wl.len and first < gl.len and std.mem.eql(u8, wl[first], gl[first])) : (first += 1) {}
    std.debug.print("\npin mismatch: {s} (line {d}; STENCIL_UPDATE_PINS=1 to re-record)\n", .{ path, first + 1 });
    const from = first -| 3;
    for (from..first) |i| try printLine(a, "  ", wl[i]);
    for (first..@min(first + 3, wl.len)) |i| try printLine(a, "- ", wl[i]);
    for (first..@min(first + 3, gl.len)) |i| try printLine(a, "+ ", gl[i]);
}

fn lines(a: std.mem.Allocator, text: []const u8) ![][]const u8 {
    var out: std.ArrayList([]const u8) = .empty;
    defer out.deinit(a);
    var it = std.mem.splitScalar(u8, text, '\n');
    while (it.next()) |l| try out.append(a, l);
    return out.toOwnedSlice(a);
}

fn printLine(a: std.mem.Allocator, mark: []const u8, line: []const u8) !void {
    var out: std.ArrayList(u8) = .empty;
    defer out.deinit(a);
    for (line) |ch| {
        if (ch >= 0x20 or ch == '\t') try out.append(a, ch) else try out.print(a, "\\x{x:0>2}", .{ch});
        if (out.items.len >= pin_cols * 4) break; // a wall of escapes helps nobody
    }
    std.debug.print("{s}{s}\n", .{ mark, out.items });
}

// ── the rendered surfaces ──────────────────────────────────────────────────────

fn renderUsage(_: std.mem.Allocator) anyerror!void {
    logo.usage();
}

fn renderBanner(_: std.mem.Allocator) anyerror!void {
    logo.banner();
}

fn renderIntro(_: std.mem.Allocator) anyerror!void {
    ui.intro();
}

fn renderHelp(_: std.mem.Allocator) anyerror!void {
    ui.help();
}

fn renderThemes(_: std.mem.Allocator) anyerror!void {
    ui.listThemes();
}

fn renderFilters(_: std.mem.Allocator) anyerror!void {
    ui.listFilters();
}

fn renderFormats(a: std.mem.Allocator) anyerror!void {
    var session = Session{ .gpa = a };
    defer session.deinit();
    ui.listFormats(&session); // no explicit pick: the A4 fallback is marked current
}

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

// ── tests ──────────────────────────────────────────────────────────────────────

test "pins: --help usage text and the logo art" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    try pin(io, "usage", renderUsage);
    try pin(io, "banner", renderBanner);
}

test "pins: console intro and /help" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    try pin(io, "intro", renderIntro);
    try pin(io, "help", renderHelp);
}

test "pins: /theme, /filter and /format listings" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    try pin(io, "themes", renderThemes);
    try pin(io, "filters", renderFilters);
    try pin(io, "formats", renderFormats);
}

test "pins: the /projects table" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    try pin(io, "projects", renderProjects);
    try pin(io, "projects-multi", renderProjectsMulti);
    try pin(io, "projects-empty", renderProjectsEmpty);
}

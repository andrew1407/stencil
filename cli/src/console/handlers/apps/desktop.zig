//! `/desktop [sync] [incognito]` and `/desktopsync [file.stencil|n|off]`: open the session in
//! the desktop app — a server project by `stencil://` link, else as a `.stencil` file — and link
//! that file so both apps edit it live (remoteEvents/fileLink.zig).
const std = @import("std");
const logo = @import("../../../app/logo.zig");
const msg = @import("../../../app/messages.zig");
const child = @import("../../../safety/child.zig");
const project = @import("../../../project.zig");
const commands = @import("../../commands.zig");
const handlers = @import("../../handlers.zig");
const ui = @import("../../ui.zig");
const fileLink = @import("../../remoteEvents/fileLink.zig");
const launch = @import("launch.zig");
const registry = @import("registry.zig");
const Session = @import("../../session.zig").Session;
const LaunchFn = @import("browser.zig").LaunchFn;

pub fn doDesktop(session: *Session, io: std.Io, arg: []const u8) !void {
    return doDesktopWith(session, io, arg, child.launch);
}

pub fn doDesktopWith(session: *Session, io: std.Io, arg: []const u8, launcher: LaunchFn) !void {
    var sync = false;
    var incognito = false;
    var it = std.mem.tokenizeAny(u8, arg, " \t");
    while (it.next()) |word| {
        if (std.ascii.eqlIgnoreCase(word, "sync")) {
            sync = true;
        } else if (std.ascii.eqlIgnoreCase(word, "incognito")) {
            incognito = true;
        } else return logo.err(msg.desktop_usage, .{word});
    }
    if (!session.hasImage()) return ui.noImage();
    if (session.hasRemote()) {
        const url = try launch.schemeUrl(session.gpa, .{ .url = session.remote_url.?, .id = session.remote_id.?, .version = session.remote_version }, incognito);
        defer session.gpa.free(url);
        try open(session, io, url, incognito, launcher);
        return;
    }
    const path = try targetPath(session);
    defer session.gpa.free(path);
    if (session.project_path == null and exists(io, path) and !confirm(session, path)) return;
    try handlers.saveProject(session, io, path);
    if (!exists(io, path)) return; // the save said why
    if (sync) try fileLink.link(session, io, path);
    if (incognito and session.app_env.desktop_bin == null) logo.note(msg.desktop_incognito_needs_bin, .{});
    try open(session, io, path, incognito, launcher);
}

/// `/desktopsync`: bare lists the desktop's linked files, `n` picks one, a path links it, `off` unlinks.
pub fn doDesktopSync(session: *Session, io: std.Io, arg: []const u8) !void {
    const a = std.mem.trim(u8, arg, " \t");
    if (a.len == 0) return list(session, io);
    if (std.ascii.eqlIgnoreCase(a, "off")) {
        if (session.file_link == null) return logo.print(msg.desktopsync_not_linked, .{});
        logo.print(msg.desktopsync_unlinked, .{session.file_link.?.path});
        return fileLink.unlink(session);
    }
    if (std.fmt.parseInt(usize, a, 10)) |n| {
        var arena = std.heap.ArenaAllocator.init(session.gpa);
        defer arena.deinit();
        const rows = try registry.load(arena.allocator(), io, session.app_env);
        if (n == 0 or n > rows.len) return logo.err(msg.desktopsync_no_such_row, .{rows.len});
        return linkFile(session, io, rows[n - 1].path);
    } else |_| {}
    if (!project.isStencilPath(a)) return logo.err(msg.desktopsync_usage, .{});
    return linkFile(session, io, a);
}

fn linkFile(session: *Session, io: std.Io, path: []const u8) !void {
    try handlers.openProject(session, io, path);
    if (!session.hasImage()) return;
    try fileLink.link(session, io, path);
    logo.print(msg.desktopsync_linked, .{path});
}

fn list(session: *Session, io: std.Io) !void {
    var arena = std.heap.ArenaAllocator.init(session.gpa);
    defer arena.deinit();
    const rows = try registry.load(arena.allocator(), io, session.app_env);
    if (rows.len == 0) return logo.print(msg.desktop_links_none, .{});
    logo.print(msg.desktop_links_head, .{});
    for (rows, 1..) |r, i| logo.print(msg.desktop_links_row, .{ i, if (r.open) "open " else "     ", r.name, r.path });
    logo.print(msg.desktop_links_hint, .{});
}

// The session's own project file, else `<label>.stencil` beside the console.
fn targetPath(session: *Session) ![]u8 {
    if (session.project_path) |p| return session.gpa.dupe(u8, p);
    return session.gpa.print("{s}.stencil", .{commands.projectBaseName(session.label orelse "project")});
}

fn exists(io: std.Io, path: []const u8) bool {
    _ = std.Io.Dir.cwd().statFile(io, path, .{}) catch return false;
    return true;
}

fn confirm(session: *Session, path: []const u8) bool {
    const ask = session.confirm_fn orelse return true;
    var q: [512]u8 = undefined;
    const question = std.fmt.bufPrint(&q, "Overwrite {s}?", .{path}) catch "Overwrite the project file?";
    if (ask(session.confirm_ctx, question)) return true;
    logo.print(msg.desktop_cancelled, .{});
    return false;
}

fn open(session: *Session, io: std.Io, target: []const u8, incognito: bool, launcher: LaunchFn) !void {
    var ob: [5][]const u8 = undefined;
    var db: [3][]const u8 = undefined;
    const argv = if (session.app_env.desktop_bin) |bin| launch.desktopArgv(bin, incognito, target, &db) else launch.openerArgv(target, &ob);
    launcher(session.gpa, io, argv) catch |e| return logo.err(msg.app_no_opener, .{@errorName(e)});
    logo.print(msg.desktop_opened, .{target});
}

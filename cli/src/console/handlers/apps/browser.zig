//! `/browser [url] [incognito]`: open the session in the browser app through the OS opener —
//! a server project by reference, else the picture and its layout inline in the launch link.
const std = @import("std");
const logo = @import("../../../app/logo.zig");
const msg = @import("../../../app/messages.zig");
const net = @import("../../../net.zig");
const child = @import("../../../safety/child.zig");
const image = @import("../../../media/image.zig");
const commands = @import("../../commands.zig");
const ui = @import("../../ui.zig");
const launch = @import("launch.zig");
const Session = @import("../../session.zig").Session;

pub const LaunchFn = *const fn (gpa: std.mem.Allocator, io: std.Io, argv: []const []const u8) child.LaunchError!void;

pub fn doBrowser(session: *Session, io: std.Io, arg: []const u8) !void {
    return doBrowserWith(session, io, arg, child.launch);
}

pub fn doBrowserWith(session: *Session, io: std.Io, arg: []const u8, launcher: LaunchFn) !void {
    var base: []const u8 = session.app_env.browser_url orelse launch.app_url;
    var incognito = false;
    var it = std.mem.tokenizeAny(u8, arg, " \t");
    while (it.next()) |word| {
        if (std.ascii.eqlIgnoreCase(word, "incognito")) {
            incognito = true;
        } else if (net.isUrl(word)) {
            base = word;
        } else return logo.err(msg.browser_usage, .{word});
    }
    if (!session.hasImage()) return ui.noImage();
    const json = try payloadJson(session, incognito);
    defer session.gpa.free(json);
    const url = try launch.browserUrl(session.gpa, base, json);
    defer session.gpa.free(url);
    if (!launch.fits(url.len)) return logo.err(msg.browser_too_big, .{ (url.len + 1023) / 1024, @min(launch.fragment_max, launch.open_max) / 1024 });
    var argv: [5][]const u8 = undefined;
    launcher(session.gpa, io, launch.openerArgv(url, &argv)) catch |e| return logo.err(msg.app_no_opener, .{@errorName(e)});
    logo.print(msg.browser_opened, .{base});
}

/// The session as a launch payload: the active server project by reference, else the original
/// picture (its source bytes, or a re-encode) with the current layout.
pub fn payloadJson(session: *Session, incognito: bool) ![]u8 {
    if (session.hasRemote()) {
        return launch.payloadJson(session.gpa, .{
            .server = .{ .url = session.remote_url.?, .id = session.remote_id.?, .version = session.remote_version },
            .incognito = incognito,
        });
    }
    const owned: ?[]u8 = if (session.source_bytes == null) try image.encode(session.gpa, session.original.?, session.default_fmt) else null;
    defer if (owned) |b| session.gpa.free(b);
    const layout = try session.currentLayoutJson();
    defer session.gpa.free(layout);
    const name = try session.gpa.print("{s}.{s}", .{ commands.projectBaseName(session.label orelse "image"), session.default_fmt.ext() });
    defer session.gpa.free(name);
    return launch.payloadJson(session.gpa, .{
        .image = session.source_bytes orelse owned.?,
        .fmt = session.default_fmt,
        .name = name,
        .layout_json = layout,
        .incognito = incognito,
    });
}

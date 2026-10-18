//! `/browser`, `/desktop` and `/desktopsync` end to end through console.handle, with the OS
//! opener replaced by a capture: what is launched, what is written, what is linked, and
//! what the user is told.
const std = @import("std");
const console = @import("../../src/console.zig");
const browser = @import("../../src/console/handlers/apps/browser.zig");
const desktop = @import("../../src/console/handlers/apps/desktop.zig");
const launch = @import("../../src/console/handlers/apps/launch.zig");
const logo = @import("../../src/app/logo.zig");
const harness = @import("console_harness.zig");
const Capture = harness.Capture;
const sample = harness.sample;
const testing = std.testing;

const Launched = struct {
    var argv: std.ArrayList([]u8) = .empty;
    var gpa: std.mem.Allocator = undefined;

    fn reset(a: std.mem.Allocator) void {
        gpa = a;
        for (argv.items) |s| a.free(s);
        argv.clearRetainingCapacity();
    }
    fn deinit() void {
        for (argv.items) |s| gpa.free(s);
        argv.deinit(gpa);
        argv = .empty;
    }
    fn launcher(_: std.mem.Allocator, _: std.Io, args: []const []const u8) !void {
        for (args) |arg| try argv.append(gpa, try gpa.dupe(u8, arg));
    }
    fn refuse(_: std.mem.Allocator, _: std.Io, _: []const []const u8) !void {
        return error.FileNotFound;
    }
};

const Rig = struct {
    a: std.mem.Allocator,
    threaded: std.Io.Threaded,
    session: console.Session,
    cap: Capture,
    in: []const u8,

    fn init(self: *Rig, name: []const u8) !void {
        self.a = testing.allocator;
        self.threaded = std.Io.Threaded.init(self.a, .{});
        self.session = console.Session{ .gpa = self.a };
        self.cap = Capture.init(self.a);
        self.cap.install();
        self.in = name;
        try std.Io.Dir.cwd().writeFile(self.io(), .{ .sub_path = name, .data = sample });
        _ = try console.handle(&self.session, self.io(), "/upload " ++ "stencil_apps_in.png");
        Launched.reset(self.a);
    }
    fn io(self: *Rig) std.Io {
        return self.threaded.io();
    }
    fn deinit(self: *Rig) void {
        std.Io.Dir.cwd().deleteFile(self.io(), self.in) catch {};
        logo.clearSink();
        self.cap.deinit();
        self.session.deinit();
        self.threaded.deinit();
        Launched.deinit();
    }
};

test "/browser opens the default app URL with the picture in the fragment; a URL word and incognito ride along" {
    var r: Rig = undefined;
    try r.init("stencil_apps_in.png");
    defer r.deinit();
    try browser.doBrowserWith(&r.session, r.io(), "", Launched.launcher);
    const url = Launched.argv.items[Launched.argv.items.len - 1];
    try testing.expect(std.mem.startsWith(u8, url, launch.app_url ++ "#stencil=%7B%22dataUrl%22%3A%22data%3Aimage%2Fpng%3Bbase64%2C"));
    try testing.expect(std.mem.indexOf(u8, url, "%22name%22%3A%22stencil_apps_in.png%22") != null);
    try testing.expect(std.mem.indexOf(u8, url, "incognito") == null);
    try testing.expect(std.mem.indexOf(u8, r.cap.text(), "opened in the browser at " ++ launch.app_url) != null);

    Launched.reset(r.a);
    try browser.doBrowserWith(&r.session, r.io(), "https://app.example incognito", Launched.launcher);
    const url2 = Launched.argv.items[Launched.argv.items.len - 1];
    try testing.expect(std.mem.startsWith(u8, url2, "https://app.example#stencil="));
    try testing.expect(std.mem.endsWith(u8, url2, "%2C%22incognito%22%3Atrue%7D"));

    Launched.reset(r.a);
    try browser.doBrowserWith(&r.session, r.io(), "nope", Launched.launcher);
    try testing.expectEqual(@as(usize, 0), Launched.argv.items.len);
    try testing.expect(std.mem.indexOf(u8, r.cap.text(), "browser takes") != null);

    try browser.doBrowserWith(&r.session, r.io(), "", Launched.refuse);
    try testing.expect(std.mem.indexOf(u8, r.cap.text(), "could not start the app (FileNotFound)") != null);
}

test "/browser: a server project goes by reference, never by bytes" {
    var r: Rig = undefined;
    try r.init("stencil_apps_in.png");
    defer r.deinit();
    try r.session.setRemote("http://h:8090", "p_7");
    r.session.remote_version = 4;
    try browser.doBrowserWith(&r.session, r.io(), "", Launched.launcher);
    const url = Launched.argv.items[Launched.argv.items.len - 1];
    try testing.expect(std.mem.endsWith(u8, url, "#stencil=%7B%22server%22%3A%7B%22url%22%3A%22http%3A%2F%2Fh%3A8090%22%2C%22id%22%3A%22p_7%22%2C%22version%22%3A4%7D%7D"));
}

test "/desktop writes <label>.stencil and opens it; sync links it; a server project opens by link" {
    var r: Rig = undefined;
    try r.init("stencil_apps_in.png");
    defer r.deinit();
    defer std.Io.Dir.cwd().deleteFile(r.io(), "stencil_apps_in.stencil") catch {};
    try desktop.doDesktopWith(&r.session, r.io(), "sync", Launched.launcher);
    try testing.expectEqualStrings("stencil_apps_in.stencil", Launched.argv.items[Launched.argv.items.len - 1]);
    try testing.expectEqualStrings("stencil_apps_in.stencil", r.session.file_link.?.path);
    try testing.expectEqualStrings("stencil_apps_in.stencil", r.session.project_path.?);
    try testing.expect(std.mem.indexOf(u8, r.cap.text(), "wrote stencil_apps_in.stencil (project)") != null);
    try testing.expect(std.mem.indexOf(u8, r.cap.text(), "opened in the desktop app: stencil_apps_in.stencil") != null);

    // The file is its own project now: a second /desktop rewrites it without asking.
    Launched.reset(r.a);
    r.session.app_env.desktop_bin = "/opt/stencil";
    try desktop.doDesktopWith(&r.session, r.io(), "incognito", Launched.launcher);
    try testing.expectEqualStrings("/opt/stencil", Launched.argv.items[0]);
    try testing.expectEqualStrings("--incognito", Launched.argv.items[1]);
    try testing.expectEqualStrings("stencil_apps_in.stencil", Launched.argv.items[2]);

    Launched.reset(r.a);
    try r.session.setRemote("http://h:8090", "p_7");
    try desktop.doDesktopWith(&r.session, r.io(), "", Launched.launcher);
    try testing.expectEqualStrings("stencil://open?server=http%3A%2F%2Fh%3A8090&id=p_7&version=0", Launched.argv.items[1]);

    _ = try console.handle(&r.session, r.io(), "/desktopsync off");
    try testing.expect(r.session.file_link == null);
    _ = try console.handle(&r.session, r.io(), "/desktopsync off");
    try testing.expect(std.mem.indexOf(u8, r.cap.text(), "no linked file") != null);
    _ = try console.handle(&r.session, r.io(), "/desktopsync x.png");
    try testing.expect(std.mem.indexOf(u8, r.cap.text(), "desktopsync takes") != null);
}

test "/desktopsync <file> loads that project and links it" {
    var r: Rig = undefined;
    try r.init("stencil_apps_in.png");
    defer r.deinit();
    defer std.Io.Dir.cwd().deleteFile(r.io(), "stencil_apps_link.stencil") catch {};
    _ = try console.handle(&r.session, r.io(), "/filter sepia");
    _ = try console.handle(&r.session, r.io(), "/save stencil_apps_link.stencil");
    _ = try console.handle(&r.session, r.io(), "/drop");
    try testing.expect(!r.session.hasImage());
    _ = try console.handle(&r.session, r.io(), "/desktopsync stencil_apps_link.stencil");
    try testing.expect(r.session.hasImage());
    try testing.expectEqualStrings("sepia", r.session.state().filter_mode);
    try testing.expectEqualStrings("stencil_apps_link.stencil", r.session.file_link.?.path);
    try testing.expect(std.mem.indexOf(u8, r.cap.text(), "linked to stencil_apps_link.stencil") != null);
}

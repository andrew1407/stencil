//! What the app hand-offs read from the environment: the browser app's URL, a desktop binary
//! to run in place of the OS opener, the desktop's state directory and the home directory.
const std = @import("std");

pub const AppEnv = struct {
    browser_url: ?[]const u8 = null,
    desktop_bin: ?[]const u8 = null,
    state_dir: ?[]const u8 = null,
    home: ?[]const u8 = null,

    pub fn fromMap(m: *const std.process.Environ.Map) AppEnv {
        return .{
            .browser_url = nonEmpty(m.get("STENCIL_BROWSER_URL")),
            .desktop_bin = nonEmpty(m.get("STENCIL_DESKTOP")),
            .state_dir = nonEmpty(m.get("STENCIL_STATE_DIR")),
            .home = nonEmpty(m.get("HOME")) orelse nonEmpty(m.get("USERPROFILE")),
        };
    }
};

fn nonEmpty(v: ?[]const u8) ?[]const u8 {
    const s = v orelse return null;
    return if (s.len == 0) null else s;
}

const testing = std.testing;

test "AppEnv: an empty variable reads as unset" {
    var m: std.process.Environ.Map = .init(testing.allocator);
    defer m.deinit();
    try m.put("STENCIL_BROWSER_URL", "");
    try m.put("STENCIL_DESKTOP", "/opt/stencil");
    try m.put("USERPROFILE", "C:\\Users\\me");
    const e = AppEnv.fromMap(&m);
    try testing.expect(e.browser_url == null);
    try testing.expectEqualStrings("/opt/stencil", e.desktop_bin.?);
    try testing.expectEqualStrings("C:\\Users\\me", e.home.?);
    try testing.expect(e.state_dir == null);
}

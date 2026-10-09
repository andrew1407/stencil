//! What the /flip and /rotate line tests read off a console session: a green line's layout and
//! whether the drawn lines or the pixels at (x, y) show it.
const std = @import("std");
const console = @import("../../src/console.zig");

pub const layout_json = "{\"lines\":[{\"points\":[{\"x\":2,\"y\":3},{\"x\":7,\"y\":1}],\"color\":\"#00ff00\",\"pointSize\":3,\"thickness\":2}]}";

pub fn has(session: *console.Session, needle: []const u8) bool {
    return std.mem.indexOf(u8, session.state().lines(), needle) != null;
}

pub fn isGreen(session: *console.Session, x: usize, y: usize) bool {
    const img = session.current().*;
    const i = (y * img.width + x) * 4;
    return img.pixels[i + 1] > 200 and img.pixels[i] < 60;
}

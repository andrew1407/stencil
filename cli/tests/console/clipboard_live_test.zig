//! The system clipboard for real, both ways, as `/copy`, `/paste` and Ctrl-S use it (clipboard.zig).
//! Opt-in: `zig build test -Dclipboard-live` on a desktop session with the platform's tools (X11
//! xclip, Wayland wl-clipboard), since a run replaces what the clipboard holds. Each copy must return
//! well inside the helper deadline: a tool forking a server that holds our pipes would wait it out.
const std = @import("std");
const clipboard = @import("../../src/clipboard.zig");
const testing = std.testing;
const sample = @embedFile("sample.png");

fn live() bool {
    return @import("test_options").clipboard_live;
}

fn msSince(io: std.Io, start: std.Io.Timestamp) i64 {
    return start.durationTo(std.Io.Clock.awake.now(io)).toMilliseconds();
}

test "live: an image goes onto the system clipboard and back, inside the deadline" {
    if (!live()) return error.SkipZigTest;
    const a = testing.allocator;
    const io = testing.io; // carries the runner's environment: DISPLAY / WAYLAND_DISPLAY reach the tools
    const start = std.Io.Clock.awake.now(io);
    try clipboard.writeImage(a, io, sample);
    try testing.expect(msSince(io, start) < 3_000);
    const back = try clipboard.readImage(a, io);
    defer a.free(back);
    try testing.expectEqualSlices(u8, sample, back);
}

test "live: a text goes onto the system clipboard and back, inside the deadline" {
    if (!live()) return error.SkipZigTest;
    const a = testing.allocator;
    const io = testing.io; // carries the runner's environment: DISPLAY / WAYLAND_DISPLAY reach the tools
    const text = "stencil — the selection, copied ✓";
    const start = std.Io.Clock.awake.now(io);
    try clipboard.writeText(a, io, text);
    try testing.expect(msSince(io, start) < 3_000);
    const back = try clipboard.readText(a, io);
    defer a.free(back);
    try testing.expectEqualStrings(text, std.mem.trimEnd(u8, back, "\n"));
}

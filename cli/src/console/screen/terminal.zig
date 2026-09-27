//! What the console borrows from the terminal, and handing it back however the process ends — a
//! normal exit, a panic, SIGTERM/SIGHUP/SIGINT/SIGQUIT: raw mode, the alternate screen, mouse
//! tracking, bracketed paste, autowrap, the skin's colours. SIGWINCH wakes the input poll.
const std = @import("std");
const skin = @import("../../app/skin.zig");

// Only write(2), tcsetattr(3) and plain loads here: `restoreTerminal` runs inside signal handlers.
var raw_fd: std.posix.fd_t = -1;
var raw_orig: std.posix.termios = undefined;
var screen_fd: std.posix.fd_t = -1;
var paste_fd: std.posix.fd_t = -1;
var installed = false;
var winch_pipe: [2]std.posix.fd_t = .{ -1, -1 };

/// The tty is in raw mode; `orig` is how it was found.
pub fn holdRaw(fd: std.posix.fd_t, orig: std.posix.termios) void {
    raw_orig = orig;
    raw_fd = fd;
    install();
}

/// The alternate screen, mouse tracking and no-autowrap are on, on `fd`.
pub fn holdScreen(fd: std.posix.fd_t) void {
    screen_fd = fd;
    install();
}

/// Bracketed paste is on, on `fd`.
pub fn holdPaste(fd: std.posix.fd_t) void {
    paste_fd = fd;
    install();
}

/// The owner handed its part back itself (the normal exit path).
pub fn releaseRaw() void {
    raw_fd = -1;
}
pub fn releaseScreen() void {
    screen_fd = -1;
}
pub fn releasePaste() void {
    paste_fd = -1;
}

/// Put back every mode still held, once; a no-op when the owners already did.
pub fn restoreTerminal() void {
    if (screen_fd >= 0) {
        const fd = screen_fd;
        screen_fd = -1;
        put(fd, "\x1b[?2026l\x1b[0m\x1b[?1002l\x1b[?1006l\x1b]117\x1b\\");
        if (skin.traitsOf(skin.get()).paints_cells) put(fd, "\x1b]110\x07\x1b]111\x07");
        put(fd, "\x1b[?7h\x1b[?1049l\x1b[?25h");
    }
    if (paste_fd >= 0) {
        put(paste_fd, "\x1b[0m\x1b[?2004l");
        paste_fd = -1;
    }
    if (raw_fd >= 0) {
        std.posix.tcsetattr(raw_fd, .FLUSH, raw_orig) catch {};
        raw_fd = -1;
    }
}

/// The read end the input poll watches beside the tty: readable after a resize (-1 = none).
pub fn resizeFd() std.posix.fd_t {
    return winch_pipe[0];
}

var resize_pending = false;

/// Swallow the resize wake-ups the poll saw, remembering that one came.
pub fn drainResize() void {
    if (winch_pipe[0] < 0) return;
    var buf: [16]u8 = undefined;
    while (std.c.read(winch_pipe[0], &buf, buf.len) > 0) resize_pending = true;
}

/// Whether the terminal was resized since the last look.
pub fn takeResize() bool {
    drainResize();
    defer resize_pending = false;
    return resize_pending;
}

fn put(fd: std.posix.fd_t, bytes: []const u8) void {
    _ = std.c.write(fd, bytes.ptr, bytes.len);
}

fn onSignal(sig: std.posix.SIG) callconv(.c) void {
    restoreTerminal();
    _ = std.c.raise(sig); // RESETHAND put the default back: the process ends as the signal meant
}

fn onResize(_: std.posix.SIG) callconv(.c) void {
    if (winch_pipe[1] >= 0) _ = std.c.write(winch_pipe[1], "w", 1);
}

fn install() void {
    if (installed) return;
    installed = true;
    const ends = [_]std.posix.SIG{ .TERM, .HUP, .INT, .QUIT };
    const act = std.posix.Sigaction{ .handler = .{ .handler = onSignal }, .mask = std.posix.sigemptyset(), .flags = std.posix.SA.RESETHAND };
    for (ends) |sig| std.posix.sigaction(sig, &act, null);
    winch_pipe = std.Io.Threaded.pipe2(.{ .NONBLOCK = true, .CLOEXEC = true }) catch return;
    const resize = std.posix.Sigaction{ .handler = .{ .handler = onResize }, .mask = std.posix.sigemptyset(), .flags = std.posix.SA.RESTART };
    std.posix.sigaction(.WINCH, &resize, null);
}

const testing = std.testing;

test "restoreTerminal: every mode still held goes back once, in one pass, then nothing" {
    const out = try std.Io.Threaded.pipe2(.{ .NONBLOCK = true });
    defer _ = std.c.close(out[0]);
    defer _ = std.c.close(out[1]);
    holdScreen(out[1]);
    holdPaste(out[1]);
    restoreTerminal();
    var buf: [256]u8 = undefined;
    const n: usize = @intCast(@max(0, std.c.read(out[0], &buf, buf.len)));
    const got = buf[0..n];
    for ([_][]const u8{ "\x1b[?2026l", "\x1b[?1002l", "\x1b[?1006l", "\x1b[?7h", "\x1b[?1049l", "\x1b[?25h", "\x1b[?2004l" }) |seq|
        try testing.expect(std.mem.indexOf(u8, got, seq) != null);
    restoreTerminal(); // already handed back: silent
    try testing.expect(std.c.read(out[0], &buf, buf.len) <= 0);
    holdScreen(out[1]);
    releaseScreen(); // the owner restored it itself
    restoreTerminal();
    try testing.expect(std.c.read(out[0], &buf, buf.len) <= 0);
}

test "a resize wakes the input poll once" {
    install();
    _ = takeResize();
    try testing.expect(resizeFd() >= 0);
    _ = std.c.raise(.WINCH);
    try testing.expect(takeResize());
    try testing.expect(!takeResize());
}

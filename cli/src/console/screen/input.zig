//! The terminal's side of the conversation: reading a byte with a timeout, asking for the
//! window size, the per-frame tick, and the mouse / reveal-speed preferences.
const sc = @import("../screen.zig");
const Screen = sc.Screen;
const std = @import("std");
const logo = @import("../../logo.zig");
const Error = sc.Screen.Error;
const reveal_speed_max = sc.reveal_speed_max;
const reveal_speed_min = sc.reveal_speed_min;
const sinkTrampoline = sc.Screen.sinkTrampoline;
const ttyWrite = sc.ttyWrite;
const reveal_speed_default = sc.reveal_speed_default;
const parseRevealSpeed = sc.parseRevealSpeed;

// One byte from `fd` within `ms`, or null (timeout / closed). Used only by the startup
// colour query — the line editor does its own polling.
pub fn readByteTimeout(fd: std.posix.fd_t, ms: i32) ?u8 {
    var pfd = [_]std.posix.pollfd{.{ .fd = fd, .events = std.posix.POLL.IN, .revents = 0 }};
    const ready = std.posix.poll(&pfd, ms) catch return null;
    if (ready == 0) return null;
    var b: [1]u8 = undefined;
    const n = std.posix.read(fd, &b) catch return null;
    return if (n == 0) null else b[0];
}

pub fn querySize(self: *Screen) Error!void {
    var ws: std.posix.winsize = undefined;
    const rc = std.c.ioctl(self.fd, @intCast(@as(u32, std.posix.T.IOCGWINSZ)), &ws);
    if (rc != 0 or ws.row == 0 or ws.col == 0) return Error.SizeUnavailable;
    self.rows = ws.row;
    self.cols = ws.col;
}

/// Re-measure the terminal; on a change, recompute geometry and repaint. Returns true
/// when it repainted (the caller then redraws the prompt). Called from the idle tick, so
/// a resize is picked up within the poll interval without a SIGWINCH handler.
pub fn tick(self: *Screen) bool {
    const or_rows = self.rows;
    const or_cols = self.cols;
    self.querySize() catch return false;
    if (self.rows == or_rows and self.cols == or_cols) return false;
    self.clampScroll();
    self.fullPaint();
    return true;
}

pub fn mouseOn(self: *Screen) bool {
    return self.mouse_on;
}

pub fn setMouse(self: *Screen, on: bool) void {
    if (on and self.mouse_on) return; // already on (a disable always re-emits, for safe teardown)
    self.mouse_on = on;
    // 1002 = button + drag-motion reporting (needed for the drag-to-highlight visual), 1006 = SGR coords.
    ttyWrite(self.fd, if (on) "\x1b[?1002h\x1b[?1006h" else "\x1b[?1002l\x1b[?1006l");
    // Whichever selection the user is left with wears the accent: ours is painted in-app,
    // the terminal's is tinted through OSC 17.
    self.setSelectionTint(!on);
    if (sc.g_screen == self) self.drawStatusBar(); // reflect the state in the rule hint
}

/// The user's standing answer to "whose selection?" — STENCIL_CONSOLE_MOUSE=on gives the
/// in-app accent one (Ctrl-S copies), =off keeps the terminal's. Null = decide per terminal.
pub fn mousePreference() ?bool {
    const raw = std.c.getenv("STENCIL_CONSOLE_MOUSE") orelse return null;
    const v = std.mem.span(raw);
    if (std.ascii.eqlIgnoreCase(v, "on") or std.mem.eql(u8, v, "1")) return true;
    if (std.ascii.eqlIgnoreCase(v, "off") or std.mem.eql(u8, v, "0")) return false;
    return null;
}

/// The user's standing answer to "how fast should text appear?" —
/// STENCIL_CONSOLE_REVEAL_SPEED=<0.01…1>, or the words `off` (= 1, instantly) and `on`
/// (= the default). Null = unset or unparseable, so the default stands.
pub fn revealSpeedPreference() ?f64 {
    const raw = std.c.getenv("STENCIL_CONSOLE_REVEAL_SPEED") orelse return null;
    const v = std.mem.span(raw);
    if (std.ascii.eqlIgnoreCase(v, "off")) return reveal_speed_max;
    if (std.ascii.eqlIgnoreCase(v, "on")) return reveal_speed_default;
    return parseRevealSpeed(v);
}

/// The speed new output currently appears at (1 = instantly, no animation).
pub fn revealSpeed(self: *Screen) f64 {
    return self.reveal_speed;
}

/// Set the appearing-text speed for the rest of the session (`/reveal-speed <speed>`).
/// Out-of-range values are clamped to the 0.01 … 1 scale.
pub fn setRevealSpeed(self: *Screen, speed: f64) void {
    self.reveal_speed = std.math.clamp(speed, reveal_speed_min, reveal_speed_max);
    // Forget the burst in progress, so the command's own confirmation line runs at the
    // NEW speed instead of taking the abbreviated burst form.
    self.reveal_last_ns = 0;
    self.reveal_burst_ns = 0;
}

/// Let the next appended output land at once, without sweeping in. Armed for the echo of a
/// typed command: those characters were already on screen as they were typed, so animating
/// them adds nothing and — at a slow speed — delays the command's real output for seconds.
pub fn skipRevealOnce(self: *Screen) void {
    self.skip_reveal_once = true;
}

/// Stand in as the current screen and the output sink without a terminal (fd -1), so a
/// test can drive what `logo.print` lands in the scrollback.
pub fn installForTest(self: *Screen) void {
    logo.setSink(sinkTrampoline, self);
    sc.g_screen = self;
}

pub fn uninstallForTest(self: *Screen) void {
    if (sc.g_screen == self) sc.g_screen = null;
    logo.clearSink();
}

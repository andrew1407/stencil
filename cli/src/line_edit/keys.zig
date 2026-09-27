//! Reading raw-mode bytes and decoding the CSI escape sequences a terminal sends for the
//! cursor keys, the mouse and bracketed paste.
const le = @import("line_edit.zig");
const Editor = le.Editor;
const std = @import("std");
const ByteResult = le.Editor.ByteResult;
const CsiResult = le.Editor.CsiResult;
const History = le.History;
const wordRight = le.wordRight;
const wordLeft = le.wordLeft;
const markerEnd = le.markerEnd;
const terminal = @import("../console/screen/terminal.zig");

pub fn nowMs(self: *Editor) i64 {
    const io = self.io orelse return 0;
    return std.Io.Clock.now(.awake, io).toMilliseconds();
}

pub fn readByte(self: *Editor) ?u8 {
    if (takeUnread(self)) |b| return b;
    var b: [1]u8 = undefined;
    const n = std.posix.read(self.fd_in, &b) catch return null;
    return if (n == 0) null else b[0];
}

// Like readByte but waits at most `timeout_ms` (−1 = forever); returns `.idle` on timeout so
// the main loop can run its idle hook between keystrokes without blocking on input.
pub fn pollByte(self: *Editor, timeout_ms: i32) ByteResult {
    if (takeUnread(self)) |b| return .{ .byte = b };
    // The resize pipe rides along, so a SIGWINCH repaints at once instead of on the next beat.
    var pfd = [_]std.posix.pollfd{
        .{ .fd = self.fd_in, .events = std.posix.POLL.IN, .revents = 0 },
        .{ .fd = terminal.resizeFd(), .events = std.posix.POLL.IN, .revents = 0 },
    };
    const ready = std.posix.poll(&pfd, timeout_ms) catch return .closed;
    if (ready == 0) return .idle;
    if (pfd[1].revents != 0) {
        terminal.drainResize();
        if (pfd[0].revents == 0) return .idle;
    }
    var b: [1]u8 = undefined;
    const n = std.posix.read(self.fd_in, &b) catch return .closed;
    return if (n == 0) .closed else .{ .byte = b[0] };
}

/// Watch the tty up to `timeout_ms` while a call runs in the background of the idle prompt:
/// true on a Ctrl-C; anything else typed is kept for the prompt (`unread`), never dropped.
pub fn pollKeep(self: *Editor, timeout_ms: i32) bool {
    var pfd = [_]std.posix.pollfd{.{ .fd = self.fd_in, .events = std.posix.POLL.IN, .revents = 0 }};
    const ready = std.posix.poll(&pfd, timeout_ms) catch return false;
    if (ready == 0) return false;
    var buf: [32]u8 = undefined;
    const n = std.posix.read(self.fd_in, &buf) catch return false;
    var cancel = false;
    for (buf[0..n]) |b| {
        if (b == 3) {
            cancel = true;
        } else if (self.unread_len < self.unread.len) {
            self.unread[self.unread_len] = b;
            self.unread_len += 1;
        }
    }
    return cancel;
}

fn takeUnread(self: *Editor) ?u8 {
    if (self.unread_len == 0) return null;
    const b = self.unread[0];
    std.mem.copyForwards(u8, self.unread[0 .. self.unread_len - 1], self.unread[1..self.unread_len]);
    self.unread_len -= 1;
    return b;
}

// The next byte within `timeout_ms`, or null — the quiet after a lone ESC.
pub fn pollNext(self: *Editor, timeout_ms: i32) ?u8 {
    return switch (self.pollByte(timeout_ms)) {
        .byte => |b| b,
        else => null,
    };
}

// Whether a byte is readable within `timeout_ms` — a peek that does NOT consume, unlike pollByte.
pub fn waitReadable(self: *Editor, timeout_ms: i32) bool {
    if (self.unread_len != 0) return true;
    var pfd = [_]std.posix.pollfd{.{ .fd = self.fd_in, .events = std.posix.POLL.IN, .revents = 0 }};
    const ready = std.posix.poll(&pfd, timeout_ms) catch return false;
    return ready > 0;
}

// A click emits a press report then a release. The double-click callback animates the logo, and the
// second click's release left queued counts as pending input and aborts it — swallow that one first.
pub fn drainMouseRelease(self: *Editor) void {
    if (!self.waitReadable(20)) return; // release not here yet (or none coming) — nothing to drain
    const b = self.readByte() orelse return;
    if (b != 27) return; // a CSI mouse report starts with ESC; anything else isn't the release
    if ((self.readByte() orelse return) != '[') return;
    if ((self.readByte() orelse return) != '<') return;
    while (self.readByte()) |c| {
        if (c == 'M' or c == 'm') break; // consumed through the report's final byte
    }
}

// A string sequence (OSC, DCS, APC, PM — `ESC ]` … already read) runs to BEL or ST (ESC \);
// consume it whole. A missing terminator ends at the first quiet moment.
pub fn drainString(self: *Editor) void {
    var budget: usize = 4096;
    while (budget > 0) : (budget -= 1) {
        const b = switch (self.pollByte(50)) {
            .byte => |c| c,
            else => return,
        };
        if (b == 7) return;
        if (b == 0x1b) {
            _ = self.pollByte(50); // the `\` of ST
            return;
        }
    }
}

// After a mouse/nav ESC '[' arrives during confirm(), consume through the sequence's final
// byte (0x40..0x7e) so the whole report is swallowed and ignored.
pub fn drainCsi(self: *Editor) void {
    while (self.readByte()) |b| {
        if (b >= 0x40 and b <= 0x7e) break;
    }
}

// Read CSI parameter bytes (digits and ';') into `out`, returning the final non-param byte.
pub fn collectCsi(self: *Editor, out: []u8) CsiResult {
    return self.collectCsi2(self.readByte() orelse 0, out);
}
// Same, but `first` is a parameter byte the caller already read (e.g. while sniffing for '<').
pub fn collectCsi2(self: *Editor, first: u8, out: []u8) CsiResult {
    var np: usize = 0;
    var bb = first;
    while ((bb >= '0' and bb <= '9') or bb == ';') {
        if (np < out.len) {
            out[np] = bb;
            np += 1;
        }
        bb = self.readByte() orelse break;
    }
    return .{ .np = np, .final = bb };
}

// Act on a collected CSI nav sequence (`ESC [ params final`, mouse excluded). A modifier param > 1
// or a Meta ESC prefix (`force_word`) turns a plain arrow into a word jump.
pub fn csi(self: *Editor, params: []const u8, final: u8, force_word: bool, prompt: []const u8, buf: []u8, len: *usize, pos: *usize, hist: *History, hidx: *usize, stash: []u8, stash_len: *usize) void {
    var it = std.mem.splitScalar(u8, params, ';');
    const code: u32 = std.fmt.parseInt(u32, it.next() orelse "", 10) catch 0;
    const mod: u32 = std.fmt.parseInt(u32, it.next() orelse "", 10) catch 0;
    const word = force_word or mod > 1; // any modifier on an arrow = move by word
    switch (final) {
        // Up/Down first move BETWEEN the rows of a wrapped line; only from the top row (Up) or the bottom
        // one (Down) do they mean the usual previous/next command.
        'A', 'B' => {
            const up = final == 'A';
            if (self.rowStep(prompt, buf[0..len.*], pos, up)) {
                self.refresh(prompt, buf[0..len.*], pos.*);
            } else {
                self.recall(prompt, buf, len, pos, hist, hidx, stash, stash_len, up);
            }
        },
        'C' => { // Right (modified = word right)
            pos.* = if (word) wordRight(buf[0..len.*], pos.*) else @min(pos.* + 1, len.*);
            self.refresh(prompt, buf[0..len.*], pos.*);
        },
        'D' => { // Left (modified = word left)
            pos.* = if (word) wordLeft(buf[0..len.*], pos.*) else pos.* -| 1;
            self.refresh(prompt, buf[0..len.*], pos.*);
        },
        'H' => {
            pos.* = 0;
            self.refresh(prompt, buf[0..len.*], pos.*);
        },
        'F' => {
            pos.* = len.*;
            self.refresh(prompt, buf[0..len.*], pos.*);
        },
        // ESC [ <code> ; <mods> u — how xterm's modifyOtherKeys and kitty report a MODIFIED key. Backspace
        // (127, or 8 where that is the erase byte) with any modifier is the delete-the-word chord.
        'u' => if (code == 127 or code == 8) {
            if (word) self.deleteWordBack(prompt, buf, len, pos) else self.backspace(prompt, buf, len, pos);
        },
        '~' => switch (code) {
            1, 7 => { // Home
                pos.* = 0;
                self.refresh(prompt, buf[0..len.*], pos.*);
            },
            4, 8 => { // End
                pos.* = len.*;
                self.refresh(prompt, buf[0..len.*], pos.*);
            },
            3 => { // Delete (modified = delete word forward)
                if (word) {
                    self.deleteWordFwd(prompt, buf, len, pos);
                } else if (pos.* < len.*) {
                    // Forward-delete drops a whole image marker too (see backspace).
                    const gone = if (markerEnd(buf[0..len.*], pos.*)) |e| e - pos.* else 1;
                    std.mem.copyForwards(u8, buf[pos.* .. len.* - gone], buf[pos.* + gone .. len.*]);
                    len.* -= gone;
                    self.syncPending(buf[0..len.*]);
                    self.refresh(prompt, buf[0..len.*], pos.*);
                }
            },
            5, 6 => if (self.screen) |s| { // Page Up / Page Down: scroll the scrollback a page
                s.scroll(code == 5, true);
                self.refresh(prompt, buf[0..len.*], pos.*);
            },
            else => {},
        },
        else => {},
    }
}

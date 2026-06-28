//! A minimal raw-mode line editor for the interactive console: left/right cursor motion,
//! backspace/delete, Home/End, Tab to complete the command word, and Up/Down to walk an
//! in-session command history. Echoing
//! is done by hand (ECHO is off) so the prompt and the leading command token render in the
//! brand accent (logo.accentSeq()). It assumes a single visible line — no wrap handling — which is
//! plenty for one-line commands. Only used when stdin is a TTY; piped input keeps the plain
//! buffered reader in console.zig, so this never runs in CI.
const std = @import("std");
const logo = @import("logo.zig");

pub const max_line = 4096; // editing buffer size; commands (URLs, crop specs) fit easily
pub const max_history = 50; // last-N entered commands kept for Up/Down

// What a readLine() call resolved to. A submitted line carries its length in `buf`; the
// other variants are key chords the caller acts on (clipboard I/O, exit) so line_edit stays
// free of session/image knowledge.
pub const Input = union(enum) {
    line: usize, // a command line of this many bytes now sits in `buf`
    eof, // Ctrl-D or a closed tty — leave the console immediately
    interrupt, // Ctrl-C — caller confirms exit (twice)
    copy, // Ctrl-Alt-C — caller copies the image to the clipboard
    paste, // Ctrl-Alt-V — caller loads an image from the clipboard
};

// ── command history (a small ring of owned strings, oldest first) ──────────────

pub const History = struct {
    gpa: std.mem.Allocator,
    items: std.ArrayList([]u8) = .empty,

    pub fn deinit(self: *History) void {
        for (self.items.items) |it| self.gpa.free(it);
        self.items.deinit(self.gpa);
    }

    /// Record a command, ignoring blanks and consecutive duplicates; drops the oldest
    /// once `max_history` is exceeded.
    pub fn add(self: *History, line: []const u8) void {
        const t = std.mem.trim(u8, line, " \t\r\n");
        if (t.len == 0) return;
        const n = self.items.items.len;
        if (n > 0 and std.mem.eql(u8, self.items.items[n - 1], t)) return;
        const dup = self.gpa.dupe(u8, t) catch return;
        self.items.append(self.gpa, dup) catch {
            self.gpa.free(dup);
            return;
        };
        if (self.items.items.len > max_history) {
            self.gpa.free(self.items.items[0]);
            _ = self.items.orderedRemove(0);
        }
    }
};

// ── the editor (raw terminal mode, restored on deinit) ─────────────────────────

pub const Editor = struct {
    fd_in: std.posix.fd_t,
    fd_out: std.posix.fd_t,
    orig: std.posix.termios,

    /// Put `tty_fd` into raw mode (no canonical line editing, no echo, no signal keys).
    pub fn init(tty_fd: std.posix.fd_t) !Editor {
        const orig = try std.posix.tcgetattr(tty_fd);
        var raw = orig;
        raw.lflag.ICANON = false;
        raw.lflag.ECHO = false;
        raw.lflag.ISIG = false; // we handle Ctrl-C / Ctrl-D ourselves
        try std.posix.tcsetattr(tty_fd, .FLUSH, raw);
        return .{ .fd_in = tty_fd, .fd_out = std.posix.STDERR_FILENO, .orig = orig };
    }

    pub fn deinit(self: *Editor) void {
        std.posix.tcsetattr(self.fd_in, .FLUSH, self.orig) catch {};
    }

    fn writeAll(self: *Editor, bytes: []const u8) void {
        var i: usize = 0;
        while (i < bytes.len) {
            const n = std.c.write(self.fd_out, bytes[i..].ptr, bytes.len - i); // libc write (no std.posix.write)
            if (n <= 0) return;
            i += @intCast(n);
        }
    }

    // Redraw the line in place: carriage-return, then the accent-coloured prompt and (only
    // when the line is a "/command") its leading token — arguments and plain text stay
    // default. Clear to end of line, then park the cursor at the visible column.
    fn refresh(self: *Editor, prompt: []const u8, line: []const u8, pos: usize) void {
        // Theme the command word only if it starts with '/'; otherwise nothing in the input.
        const cmd_end: usize = if (line.len != 0 and line[0] == '/')
            (std.mem.indexOfAny(u8, line, " \t") orelse line.len)
        else
            0;
        self.writeAll("\r");
        self.writeAll(logo.accentSeq());
        self.writeAll(prompt);
        self.writeAll(line[0..cmd_end]);
        self.writeAll(logo.resetSeq());
        self.writeAll(line[cmd_end..]);
        self.writeAll("\x1b[K");
        self.writeAll("\r");
        const vis = prompt.len + pos;
        if (vis > 0) {
            var fbuf: [16]u8 = undefined;
            self.writeAll(std.fmt.bufPrint(&fbuf, "\x1b[{d}C", .{vis}) catch return);
        }
    }

    fn readByte(self: *Editor) ?u8 {
        var b: [1]u8 = undefined;
        const n = std.posix.read(self.fd_in, &b) catch return null;
        return if (n == 0) null else b[0];
    }

    /// Read one edited line into `buf`. Returns a submitted `.line` (its length), or a key
    /// chord the caller handles — `.eof` (Ctrl-D / closed tty), `.interrupt` (Ctrl-C, exit),
    /// `.copy` (Ctrl-Alt-C) or `.paste` (Ctrl-Alt-V). The Alt-modified chords arrive as an
    /// ESC prefix (the Meta convention) followed by the Ctrl byte. `armed` carries the
    /// two-Ctrl-C exit guard across calls: any key but Ctrl-C disarms it, so the caller can
    /// require two presses to leave. `completions` are command names for Tab-complete.
    pub fn readLine(self: *Editor, prompt: []const u8, buf: []u8, hist: *History, completions: []const []const u8, armed: *bool) Input {
        var len: usize = 0;
        var pos: usize = 0;
        var hidx: usize = hist.items.items.len; // == items.len means "the fresh line"
        var stash: [max_line]u8 = undefined; // the in-progress line, parked while browsing
        var stash_len: usize = 0;
        self.refresh(prompt, buf[0..0], 0);

        while (true) {
            const ch = self.readByte() orelse {
                if (len == 0) return .eof; // closed tty -> end session
                continue;
            };
            if (ch != 3) armed.* = false; // any key but Ctrl-C disarms the exit guard
            switch (ch) {
                '\r', '\n' => {
                    self.writeAll("\r\n");
                    return .{ .line = len };
                },
                3 => { // Ctrl-C: confirm exit (the caller requires two presses)
                    self.writeAll("\r\n");
                    return .interrupt;
                },
                4 => { // Ctrl-D (EOF): leave the console
                    self.writeAll("\r\n");
                    return .eof;
                },
                21 => { // Ctrl-U: clear the line
                    len = 0;
                    pos = 0;
                    self.refresh(prompt, buf[0..0], 0);
                },
                1 => { // Ctrl-A: home
                    pos = 0;
                    self.refresh(prompt, buf[0..len], pos);
                },
                5 => { // Ctrl-E: end
                    pos = len;
                    self.refresh(prompt, buf[0..len], pos);
                },
                9 => self.complete(prompt, buf, &len, &pos, completions), // Tab
                8, 127 => if (pos > 0) { // backspace
                    std.mem.copyForwards(u8, buf[pos - 1 .. len - 1], buf[pos..len]);
                    pos -= 1;
                    len -= 1;
                    self.refresh(prompt, buf[0..len], pos);
                },
                27 => { // ESC: a nav sequence (arrows/Home/End/Del), or an Alt-modified chord
                    const nxt = self.readByte() orelse continue; // lone ESC: ignore
                    switch (nxt) {
                        3 => { // Ctrl-Alt-C: copy the image to the clipboard
                            self.writeAll("\r\n");
                            return .copy;
                        },
                        22 => { // Ctrl-Alt-V: paste an image from the clipboard
                            self.writeAll("\r\n");
                            return .paste;
                        },
                        '[', 'O' => self.escape(prompt, buf, &len, &pos, hist, &hidx, &stash, &stash_len),
                        else => {}, // other Alt-combo: ignore
                    }
                },
                else => if (ch >= 0x20 and len < buf.len) { // printable: insert at cursor
                    if (pos < len) std.mem.copyBackwards(u8, buf[pos + 1 .. len + 1], buf[pos..len]);
                    buf[pos] = ch;
                    pos += 1;
                    len += 1;
                    self.refresh(prompt, buf[0..len], pos);
                },
            }
        }
    }

    /// Ask a yes/no question on the raw-mode tty and read a single keypress. 'y' or Enter
    /// confirm (yes is the default); 'n', Esc or Ctrl-C decline. Used to guard `/upload`.
    pub fn confirm(self: *Editor, question: []const u8) bool {
        self.writeAll(logo.accentSeq());
        self.writeAll(question);
        self.writeAll(" (Y/n) ");
        self.writeAll(logo.resetSeq());
        while (true) {
            const ch = self.readByte() orelse return false; // closed tty -> treat as decline
            switch (ch) {
                'y', 'Y', '\r', '\n' => {
                    self.writeAll("yes\r\n");
                    return true;
                },
                'n', 'N', 27, 3 => { // 'n', Esc or Ctrl-C
                    self.writeAll("no\r\n");
                    return false;
                },
                else => {},
            }
        }
    }

    // Handle an ESC-introduced navigation sequence (arrow keys, Home/End, Delete). The caller
    // (readLine) has already consumed the ESC and the '[' / 'O' intro byte.
    fn escape(self: *Editor, prompt: []const u8, buf: []u8, len: *usize, pos: *usize, hist: *History, hidx: *usize, stash: []u8, stash_len: *usize) void {
        const code = self.readByte() orelse return;
        switch (code) {
            'A' => self.recall(prompt, buf, len, pos, hist, hidx, stash, stash_len, true),
            'B' => self.recall(prompt, buf, len, pos, hist, hidx, stash, stash_len, false),
            'C' => if (pos.* < len.*) {
                pos.* += 1;
                self.refresh(prompt, buf[0..len.*], pos.*);
            },
            'D' => if (pos.* > 0) {
                pos.* -= 1;
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
            '3' => { // Delete: consume the trailing '~', then drop the char under the cursor
                _ = self.readByte();
                if (pos.* < len.*) {
                    std.mem.copyForwards(u8, buf[pos.* .. len.* - 1], buf[pos.* + 1 .. len.*]);
                    len.* -= 1;
                    self.refresh(prompt, buf[0..len.*], pos.*);
                }
            },
            else => {},
        }
    }

    // Up (older) / Down (newer) through history, stashing the fresh line on first Up.
    fn recall(self: *Editor, prompt: []const u8, buf: []u8, len: *usize, pos: *usize, hist: *History, hidx: *usize, stash: []u8, stash_len: *usize, older: bool) void {
        const items = hist.items.items;
        if (older) {
            if (hidx.* == 0) return;
            if (hidx.* == items.len) { // leaving the fresh line — park it
                const m = @min(len.*, stash.len);
                @memcpy(stash[0..m], buf[0..m]);
                stash_len.* = m;
            }
            hidx.* -= 1;
            len.* = copyInto(buf, items[hidx.*]);
        } else {
            if (hidx.* >= items.len) return;
            hidx.* += 1;
            if (hidx.* == items.len) {
                @memcpy(buf[0..stash_len.*], stash[0..stash_len.*]);
                len.* = stash_len.*;
            } else {
                len.* = copyInto(buf, items[hidx.*]);
            }
        }
        pos.* = len.*;
        self.refresh(prompt, buf[0..len.*], pos.*);
    }

    // Tab-complete the command word against `completions`. Only fires while still typing the
    // command (no whitespace yet, cursor at the end). A unique match fills it in with a
    // trailing space; several matches extend to their common prefix, or list them if that
    // adds nothing. A leading '/' is preserved. Names are matched case-insensitively.
    fn complete(self: *Editor, prompt: []const u8, buf: []u8, len: *usize, pos: *usize, completions: []const []const u8) void {
        const line = buf[0..len.*];
        if (pos.* != len.* or std.mem.indexOfAny(u8, line, " \t") != null) return;
        const has_slash = line.len != 0 and line[0] == '/';
        const base = if (has_slash) line[1..] else line;

        var count: usize = 0;
        var only: []const u8 = "";
        var lcp: []const u8 = "";
        for (completions) |cand| {
            if (cand.len < base.len or !std.ascii.eqlIgnoreCase(cand[0..base.len], base)) continue;
            lcp = if (count == 0) cand else lcp[0..commonLen(lcp, cand)];
            only = cand;
            count += 1;
        }
        if (count == 0) return;

        if (count == 1) {
            len.* = setCommand(buf, has_slash, only, true); // unique: fill in + trailing space
        } else if (lcp.len > base.len) {
            len.* = setCommand(buf, has_slash, lcp, false); // extend to the common prefix
        } else {
            self.listMatches(completions, base); // ambiguous: show the options
        }
        pos.* = len.*;
        self.refresh(prompt, buf[0..len.*], pos.*);
    }

    fn listMatches(self: *Editor, completions: []const []const u8, base: []const u8) void {
        self.writeAll("\r\n");
        for (completions) |cand| {
            if (cand.len >= base.len and std.ascii.eqlIgnoreCase(cand[0..base.len], base)) {
                self.writeAll(cand);
                self.writeAll("  ");
            }
        }
        self.writeAll("\r\n");
    }
};

// Write "[/]name[ ]" into buf and return the new length (clamped to the buffer).
fn setCommand(buf: []u8, slash: bool, name: []const u8, space: bool) usize {
    var i: usize = 0;
    if (slash and buf.len != 0) {
        buf[0] = '/';
        i = 1;
    }
    const n = @min(name.len, buf.len - i);
    @memcpy(buf[i .. i + n], name[0..n]);
    i += n;
    if (space and i < buf.len) {
        buf[i] = ' ';
        i += 1;
    }
    return i;
}

// Length of the common case-insensitive prefix of `a` and `b`.
fn commonLen(a: []const u8, b: []const u8) usize {
    const n = @min(a.len, b.len);
    var i: usize = 0;
    while (i < n and std.ascii.toLower(a[i]) == std.ascii.toLower(b[i])) : (i += 1) {}
    return i;
}

fn copyInto(buf: []u8, src: []const u8) usize {
    const n = @min(buf.len, src.len);
    @memcpy(buf[0..n], src[0..n]);
    return n;
}

const testing = std.testing;

test "History.add: trims, dedups consecutive, caps at max_history" {
    var h = History{ .gpa = testing.allocator };
    defer h.deinit();

    h.add("  /upload a.png  ");
    h.add("/upload a.png"); // consecutive duplicate — ignored
    h.add("   "); // blank — ignored
    h.add("/rotate 1");
    try testing.expectEqual(@as(usize, 2), h.items.items.len);
    try testing.expectEqualStrings("/upload a.png", h.items.items[0]);
    try testing.expectEqualStrings("/rotate 1", h.items.items[1]);

    var i: usize = 0;
    while (i < max_history + 10) : (i += 1) {
        var b: [16]u8 = undefined;
        h.add(std.fmt.bufPrint(&b, "/cmd {d}", .{i}) catch unreachable);
    }
    try testing.expectEqual(@as(usize, max_history), h.items.items.len);
}

test "completion helpers: common prefix and command fill-in" {
    try testing.expectEqual(@as(usize, 2), commonLen("reset", "redo")); // "re"
    try testing.expectEqual(@as(usize, 0), commonLen("crop", "save"));
    try testing.expectEqual(@as(usize, 6), commonLen("ROTATE", "rotate")); // case-insensitive

    var buf: [32]u8 = undefined;
    try testing.expectEqualStrings("/upload ", buf[0..setCommand(&buf, true, "upload", true)]);
    try testing.expectEqualStrings("rotate", buf[0..setCommand(&buf, false, "rotate", false)]);
}

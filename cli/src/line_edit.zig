//! A minimal raw-mode line editor for the interactive console: left/right cursor motion,
//! backspace/delete, Home/End, Tab to complete the command word, and Up/Down to walk an
//! in-session command history. Echoing
//! is done by hand (ECHO is off) so the prompt and the leading command token render in the
//! brand accent (logo.accentSeq()). It assumes a single visible line — no wrap handling — which is
//! plenty for one-line commands. Only used when stdin is a TTY; piped input keeps the plain
//! buffered reader in console.zig, so this never runs in CI.
const std = @import("std");
const logo = @import("logo.zig");
const screen_mod = @import("console/screen.zig");

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
    // Optional idle hook: invoked when the input read times out (no key for ~idle_ms) so the REPL
    // can poll the live events feed and surface a peer's change while the user sits at the prompt.
    // readLine clears the prompt line before the call and redraws it after.
    idle_cb: ?*const fn (*anyopaque) bool = null,  // returns true if it printed → repaint the prompt
    idle_ctx: ?*anyopaque = null,

    // Full-screen ("screen mode") wiring, all null in the plain line-oriented mode. When
    // `screen` is set the prompt is drawn at its fixed bottom row (rather than in place with a
    // bare '\r') and mouse wheel / logo clicks drive the screen directly.
    screen: ?*screen_mod.Screen = null,
    io: ?std.Io = null, // for double-click timing (monotonic clock)
    // Single-click on the logo runs `logo_cycle_cb` (advance the accent); a second click within
    // `double_click_ms` runs `logo_custom_cb` (set a random custom colour). Both share `logo_ctx`.
    logo_cycle_cb: ?*const fn (*anyopaque) void = null,
    logo_custom_cb: ?*const fn (*anyopaque) void = null,
    logo_ctx: ?*anyopaque = null,
    // Called with the text of a completed drag-selection so the caller can copy it to the clipboard.
    copy_text_cb: ?*const fn (*anyopaque, []const u8) void = null,

    const ByteResult = union(enum) { byte: u8, idle, closed };
    const double_click_ms: i64 = 500; // two logo clicks within this window = double-click (a
    // single click is deferred this long before it cycles, so a slower double-click still lands)

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
        self.gotoLineStart();
        self.writeAll(logo.accentReal());
        self.writeAll(prompt);
        self.writeAll(line[0..cmd_end]);
        self.writeAll(logo.resetSeq());
        self.writeAll(line[cmd_end..]);
        self.writeAll("\x1b[K");
        self.gotoLineStart();
        const vis = prompt.len + pos;
        if (vis > 0) {
            var fbuf: [16]u8 = undefined;
            self.writeAll(std.fmt.bufPrint(&fbuf, "\x1b[{d}C", .{vis}) catch return);
        }
    }

    // Park the cursor at column 1 of the input line: the fixed prompt row in screen mode,
    // otherwise the current row (a bare carriage return), matching the legacy behaviour.
    fn gotoLineStart(self: *Editor) void {
        if (self.screen) |s| {
            var b: [16]u8 = undefined;
            self.writeAll(std.fmt.bufPrint(&b, "\x1b[{d};1H", .{s.promptRow()}) catch "\r");
        } else {
            self.writeAll("\r");
        }
    }

    // End the current input line. In screen mode the prompt is a fixed bottom row, so a real
    // newline would line-feed and scroll the whole alt-screen (eating the pinned header) — so
    // instead just clear the prompt row in place; the command is echoed into the scrollback by
    // the caller. In the plain editor, emit the usual CR+LF to advance to the next line.
    fn endPromptLine(self: *Editor) void {
        if (self.screen != null) {
            self.gotoLineStart();
            self.writeAll("\x1b[2K");
        } else {
            self.writeAll("\r\n");
        }
    }

    fn nowMs(self: *Editor) i64 {
        const io = self.io orelse return 0;
        return std.Io.Clock.now(.awake, io).toMilliseconds();
    }

    fn readByte(self: *Editor) ?u8 {
        var b: [1]u8 = undefined;
        const n = std.posix.read(self.fd_in, &b) catch return null;
        return if (n == 0) null else b[0];
    }

    // Like readByte but waits at most `timeout_ms` (−1 = forever); returns `.idle` on timeout so
    // the main loop can run its idle hook between keystrokes without blocking on input.
    fn pollByte(self: *Editor, timeout_ms: i32) ByteResult {
        var pfd = [_]std.posix.pollfd{.{ .fd = self.fd_in, .events = std.posix.POLL.IN, .revents = 0 }};
        const ready = std.posix.poll(&pfd, timeout_ms) catch return .closed;
        if (ready == 0) return .idle;
        var b: [1]u8 = undefined;
        const n = std.posix.read(self.fd_in, &b) catch return .closed;
        return if (n == 0) .closed else .{ .byte = b[0] };
    }

    // Whether a byte is readable within `timeout_ms` — a peek that does NOT consume, unlike pollByte.
    fn waitReadable(self: *Editor, timeout_ms: i32) bool {
        var pfd = [_]std.posix.pollfd{.{ .fd = self.fd_in, .events = std.posix.POLL.IN, .revents = 0 }};
        const ready = std.posix.poll(&pfd, timeout_ms) catch return false;
        return ready > 0;
    }

    // A physical click emits a press report (…M) and then a release report (…m). When a double-click
    // fires the custom-colour callback — which animates the logo — the second click's release is
    // still queued; left there it counts as "pending input" and aborts the flourish the instant it
    // starts (screen.sleepOrAbort). Swallow that one release first. A press is always followed by its
    // own release before any later click's press, so consuming a single report can never drop a click.
    fn drainMouseRelease(self: *Editor) void {
        if (!self.waitReadable(20)) return; // release not here yet (or none coming) — nothing to drain
        const b = self.readByte() orelse return;
        if (b != 27) return; // a CSI mouse report starts with ESC; anything else isn't the release
        if ((self.readByte() orelse return) != '[') return;
        if ((self.readByte() orelse return) != '<') return;
        while (self.readByte()) |c| {
            if (c == 'M' or c == 'm') break; // consumed through the report's final byte
        }
    }

    /// Read one edited line into `buf`. Returns a submitted `.line` (its length), or a key
    /// chord the caller handles — `.eof` (Ctrl-D / closed tty), `.interrupt` (Ctrl-C, exit),
    /// `.copy` (Ctrl-Alt-C) or `.paste` (Ctrl-Alt-V). The Alt-modified chords arrive as an
    /// ESC prefix (the Meta convention) followed by the Ctrl byte. `armed` carries the
    /// two-Ctrl-C exit guard across calls: any key but Ctrl-C disarms it, so the caller can
    /// require two presses to leave. `completions` are command names for Tab-complete.
    pub fn readLine(self: *Editor, prompt: []const u8, buf: []u8, hist: *History, completions: []const []const u8, armed: *bool, preset: []const u8) Input {
        var len: usize = @min(preset.len, buf.len);
        if (len != 0) @memcpy(buf[0..len], preset[0..len]); // start with any prefilled text
        var pos: usize = len;
        var hidx: usize = hist.items.items.len; // == items.len means "the fresh line"
        var stash: [max_line]u8 = undefined; // the in-progress line, parked while browsing
        var stash_len: usize = 0;
        // Logo click debounce: a single click is deferred by `double_click_ms` so a second click
        // can supersede it as a double-click. `click_pending` is armed on the first click and
        // fired (cycle the accent) once the window lapses with no second click.
        var click_pending = false;
        var click_at: i64 = 0;
        self.refresh(prompt, buf[0..len], pos);

        while (true) {
            // Timeout: short while a logo click is pending (so it resolves promptly), else the
            // 500ms idle-hook cadence, else block indefinitely.
            const timeout: i32 = if (click_pending) blk: {
                const rem = double_click_ms - (self.nowMs() - click_at);
                break :blk if (rem <= 0) 1 else @intCast(@min(rem, @as(i64, 500)));
            } else if (self.idle_cb != null) 500 else -1;
            const ch = blk: {
                switch (self.pollByte(timeout)) {
                    .closed => {
                        if (len == 0) return .eof;
                        continue;
                    },
                    // Only repaint when the hook actually printed something (it clears the line
                    // itself first) — otherwise stay silent so the idle prompt never flickers.
                    .idle => {
                        // A pending logo click that outlived the double-click window is a single click.
                        if (click_pending and self.nowMs() - click_at >= double_click_ms) {
                            click_pending = false;
                            if (self.logo_cycle_cb) |cb| cb(self.logo_ctx.?);
                            self.refresh(prompt, buf[0..len], pos);
                            continue;
                        }
                        if (self.idle_cb) |cb| {
                            if (cb(self.idle_ctx.?)) self.refresh(prompt, buf[0..len], pos);
                        }
                        continue;
                    },
                    .byte => |b| break :blk b,
                }
            };
            if (ch != 3) armed.* = false; // any key but Ctrl-C disarms the exit guard
            switch (ch) {
                '\r', '\n' => {
                    self.endPromptLine();
                    return .{ .line = len };
                },
                3 => { // Ctrl-C: copy an active text selection if there is one, else confirm exit
                    if (self.screen) |s| {
                        if (s.hasSelection()) {
                            const text = s.takeSelection();
                            if (text.len != 0) {
                                if (self.copy_text_cb) |cb| cb(self.logo_ctx.?, text);
                            }
                            self.refresh(prompt, buf[0..len], pos);
                            continue;
                        }
                    }
                    self.endPromptLine();
                    return .interrupt;
                },
                4 => { // Ctrl-D (EOF): leave the console
                    self.endPromptLine();
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
                27 => { // ESC: a nav/mouse sequence, or an Alt-modified chord
                    const nxt = self.readByte() orelse continue; // lone ESC: ignore
                    switch (nxt) {
                        3 => { // Ctrl-Alt-C: copy the image to the clipboard
                            self.endPromptLine();
                            return .copy;
                        },
                        22 => { // Ctrl-Alt-V: paste an image from the clipboard
                            self.endPromptLine();
                            return .paste;
                        },
                        '[' => {
                            const b2 = self.readByte() orelse continue;
                            if (b2 == '<') // SGR mouse report
                                self.handleMouse(prompt, buf, &len, &pos, &click_pending, &click_at)
                            else
                                self.escape(b2, prompt, buf, &len, &pos, hist, &hidx, &stash, &stash_len);
                        },
                        'O' => {
                            const code = self.readByte() orelse continue;
                            self.escape(code, prompt, buf, &len, &pos, hist, &hidx, &stash, &stash_len);
                        },
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
        if (self.screen != null) { // draw the question on the fixed prompt row
            self.gotoLineStart();
            self.writeAll("\x1b[2K");
        }
        self.writeAll(logo.accentReal());
        self.writeAll(question);
        self.writeAll(" (Y/n) ");
        self.writeAll(logo.resetSeq());
        while (true) {
            const ch = self.readByte() orelse return false; // closed tty -> treat as decline
            switch (ch) {
                'y', 'Y', '\r', '\n' => {
                    self.finishConfirm(question, true);
                    return true;
                },
                'n', 'N', 3 => { // 'n' or Ctrl-C
                    self.finishConfirm(question, false);
                    return false;
                },
                27 => { // Esc declines — but in screen mode a mouse report also starts with ESC,
                    // so swallow a trailing CSI (ESC '[' … final) and ignore it rather than decline.
                    if (self.screen != null) {
                        switch (self.pollByte(2)) {
                            .byte => |b2| if (b2 == '[') {
                                self.drainCsi();
                                continue;
                            },
                            else => {}, // lone Esc → fall through to decline
                        }
                    }
                    self.finishConfirm(question, false);
                    return false;
                },
                else => {},
            }
        }
    }

    // After a mouse/nav ESC '[' arrives during confirm(), consume through the sequence's final
    // byte (0x40..0x7e) so the whole report is swallowed and ignored.
    fn drainCsi(self: *Editor) void {
        while (self.readByte()) |b| {
            if (b >= 0x40 and b <= 0x7e) break;
        }
    }

    fn finishConfirm(self: *Editor, question: []const u8, yes: bool) void {
        if (self.screen != null) {
            logo.print("{s} {s}\n", .{ question, if (yes) "yes" else "no" }); // record in scrollback
            self.gotoLineStart();
            self.writeAll("\x1b[2K");
        } else {
            self.writeAll(if (yes) "yes\r\n" else "no\r\n");
        }
    }

    // Handle an ESC-introduced navigation sequence (arrow keys, Home/End, Delete, Page Up/Down).
    // The caller (readLine) has already consumed the ESC, the '[' / 'O' intro byte, and `code`.
    fn escape(self: *Editor, code: u8, prompt: []const u8, buf: []u8, len: *usize, pos: *usize, hist: *History, hidx: *usize, stash: []u8, stash_len: *usize) void {
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
            '5', '6' => { // Page Up / Page Down: scroll the screen-mode scrollback a page
                _ = self.readByte(); // consume the trailing '~'
                if (self.screen) |s| {
                    s.scroll(code == '5', true);
                    self.refresh(prompt, buf[0..len.*], pos.*);
                }
            },
            else => {},
        }
    }

    // Handle an SGR mouse report (`ESC [ <` already consumed): read up to the final 'M'/'m',
    // parse it, and act — wheel scrolls the scrollback; a left-click on the pinned logo ARMS a
    // deferred single-click (cycle, fired by readLine after the double-click window), and a
    // *second* click within that window supersedes it as a double-click (random custom colour).
    // Deferring avoids the first click's animation blocking the double-click detection. Else ignored.
    fn handleMouse(self: *Editor, prompt: []const u8, buf: []u8, len: *usize, pos: *usize, click_pending: *bool, click_at: *i64) void {
        var mb: [32]u8 = undefined;
        var mi: usize = 0;
        while (mi < mb.len) {
            const b = self.readByte() orelse break;
            mb[mi] = b;
            mi += 1;
            if (b == 'M' or b == 'm') break;
        }
        const ev = screen_mod.parseMouse(mb[0..mi]) orelse return;
        const s = self.screen orelse return;
        if (ev.isWheelUp()) {
            s.scroll(true, false);
            self.refresh(prompt, buf[0..len.*], pos.*);
        } else if (ev.isWheelDown()) {
            s.scroll(false, false);
            self.refresh(prompt, buf[0..len.*], pos.*);
        } else if (ev.isLeftDrag()) {
            s.selDrag(ev.col, ev.row); // extend a text selection
            self.refresh(prompt, buf[0..len.*], pos.*);
        } else if (ev.isRelease()) {
            if (s.selActive()) { // finished a drag → keep the highlight; Ctrl+C copies it
                s.selEnd();
                self.refresh(prompt, buf[0..len.*], pos.*);
            }
        } else if (ev.isLeftPress()) {
            if (s.inHeader(ev.row)) { // logo click: deferred single (cycle) / double (custom)
                const now = self.nowMs();
                if (click_pending.* and now - click_at.* <= double_click_ms) {
                    click_pending.* = false;
                    self.drainMouseRelease(); // eat this click's trailing release so the flourish plays
                    if (self.logo_custom_cb) |cb| cb(self.logo_ctx.?);
                    self.refresh(prompt, buf[0..len.*], pos.*);
                } else {
                    click_pending.* = true;
                    click_at.* = now;
                }
            } else { // press on an output row → begin a text selection
                s.selStart(ev.col, ev.row);
                self.refresh(prompt, buf[0..len.*], pos.*);
            }
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
        // In screen mode, emit into the scrollback (one line) so the frame/header stay put;
        // the caller redraws the prompt afterwards. Otherwise print inline under the prompt.
        if (self.screen != null) {
            for (completions) |cand| {
                if (cand.len >= base.len and std.ascii.eqlIgnoreCase(cand[0..base.len], base))
                    logo.print("{s}  ", .{cand});
            }
            logo.print("\n", .{});
            return;
        }
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

//! A minimal raw-mode line editor for the interactive console: left/right cursor motion,
//! backspace/delete, Home/End, Tab to complete the command word, and Up/Down to walk an
//! in-session command history. Ctrl-V attaches a clipboard image to the line being typed,
//! shown inline as an `[Image #N <name>]` marker — the editor owns the markers, the host
//! (console.zig) owns the pictures behind them, through the PendingImages hooks. Echoing
//! is done by hand (ECHO is off) so the prompt and the leading command token render in the
//! brand accent (logo.accentSeq()). It assumes a single visible line — no wrap handling — which is
//! plenty for one-line commands. Only used when stdin is a TTY; piped input keeps the plain
//! buffered reader in console.zig, so this never runs in CI.
const std = @import("std");
const logo = @import("logo.zig");
const screen_mod = @import("console/screen.zig");

pub const max_line = 4096; // editing buffer size; commands (URLs, crop specs) fit easily
pub const max_history = 50; // last-N entered commands kept for Up/Down

/// How many images one prompt line may carry (the cap Claude Code's CLI uses; a turn can
/// still hold more by /upload-ing as well).
pub const max_pending_images = 3;
/// The widest name a marker shows — the host elides longer ones into this.
pub const max_marker_label = 20;
// What a pasted image leaves in the line: "[Image #N <label>]", the index at a fixed offset
// so dropping one renumbers the rest with a single byte write.
const marker_open = "[Image #";

// What a readLine() call resolved to. A submitted line carries its length in `buf`; the
// other variants are key chords the caller acts on (clipboard I/O, exit) so line_edit stays
// free of session/image knowledge.
pub const Input = union(enum) {
    line: usize, // a command line of this many bytes now sits in `buf`
    eof, // Ctrl-D or a closed tty — leave the console immediately
    interrupt, // Ctrl-C — caller confirms exit (twice)
    copy, // Ctrl-Alt-C — caller copies the image to the clipboard
    paste, // Ctrl-V (or Ctrl-Alt-V) — caller loads an image from the clipboard
    unpaste, // Ctrl-Z (or Ctrl-Alt-Z) — caller takes back the last image added this turn
};

/// How the images pasted INTO the line being typed are reached: the editor owns the
/// `[Image #N …]` markers in the text, the host owns the picture behind each one. With no
/// hooks wired (piped input, tests) Ctrl-V falls back to returning `.paste`.
/// What a Ctrl-V found on the clipboard: a picture (label length), plain text (byte length),
/// or nothing at all.
pub const PasteResult = union(enum) { image: usize, text: usize, none };

pub const PendingImages = struct {
    ctx: *anyopaque,
    /// What Ctrl-V does with the clipboard, for the line `before` — which also says how many
    /// images that line may carry (`/upload` loads one picture, a `/prompt` up to three; the
    /// host knows the verbs). A picture is held and its marker label written into `label`; with
    /// no picture the clipboard's TEXT is written into `text` and simply typed into the line.
    /// `.none` means there was nothing to take (the hook said why).
    paste: *const fn (ctx: *anyopaque, before: []const u8, label: []u8, text: []u8) PasteResult,
    /// Consider a bracketed paste — `text`, typed after `before` — as an image file's path;
    /// same contract, and silent when it declines (the paste is then ordinary text).
    addPath: *const fn (ctx: *anyopaque, text: []const u8, before: []const u8, label: []u8) ?usize,
    /// Keep only these images (0-based, in the order the line's markers now read).
    keep: *const fn (ctx: *anyopaque, kept: []const usize) void,
    count: *const fn (ctx: *anyopaque) usize,
};

/// How many rows `len` bytes occupy when the first row holds `first` and the rest hold `cols`.
/// Always at least 1 (an empty line still owns its row).
fn wrappedRows(len: usize, first: usize, cols: usize) usize {
    if (len <= first) return 1;
    const rest = len - first;
    return 1 + (rest + cols - 1) / cols;
}

/// The slice of `line` shown on wrapped row `idx` (0-based).
fn rowSlice(line: []const u8, idx: usize, first: usize, cols: usize) []const u8 {
    if (idx == 0) return line[0..@min(first, line.len)];
    const start = first + (idx - 1) * cols;
    if (start >= line.len) return line[line.len..];
    return line[start..@min(start + cols, line.len)];
}

/// Byte offset into `line` where wrapped row `idx` begins.
fn rowStart(idx: usize, first: usize, cols: usize) usize {
    return if (idx == 0) 0 else first + (idx - 1) * cols;
}

/// Where the cursor lands after moving one wrapped row up (`up`) or down, keeping the SCREEN
/// column — row 0 is indented by the prompt, the rest start at column 1. Null when there is no
/// row that way, which is the caller's signal to fall back to history recall: on a line that
/// fits one row (the common case) Up/Down keep meaning "previous/next command". Pure, so the
/// geometry unit-tests without a terminal.
fn rowMove(len: usize, pos: usize, prompt_len: usize, first: usize, cols: usize, up: bool) ?usize {
    const rows = wrappedRows(len, first, cols);
    if (rows == 1) return null;
    const cur = wrappedRows(pos + 1, first, cols) - 1;
    if (up and cur == 0) return null;
    if (!up and cur + 1 >= rows) return null;
    const target = if (up) cur - 1 else cur + 1;
    const screen_col = (pos - rowStart(cur, first, cols)) + if (cur == 0) prompt_len else 0;
    const want = screen_col -| (if (target == 0) prompt_len else 0);
    const width = if (target == 0) first else cols;
    return @min(rowStart(target, first, cols) + @min(want, width), len);
}

// ── command history (a small ring of owned strings, oldest first) ──────────────

/// The tallest the wrapped input block can grow (screen.maxPromptRows clamps to this too).
pub const max_prompt_rows = 8;

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
    // The byte this tty sends for a plain Backspace (termios VERASE — 0x7f nearly everywhere,
    // 0x08 on the few that use it). Whichever it is NOT, the other means Ctrl-Backspace, which
    // every desktop expects to delete a WORD. Defaults to DEL so a hand-built editor (tests)
    // reads 0x08 as the word chord.
    erase: u8 = 127,
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
    // Ctrl-S: called with the visual selection's text to copy it to the clipboard.
    copy_text_cb: ?*const fn (*anyopaque, []const u8) void = null,
    // Images pasted into the line being typed (Ctrl-V / a pasted image path); null in the
    // piped and test paths, where Ctrl-V stays the plain `.paste` chord.
    pending: ?PendingImages = null,
    // Scratch for the input rows handed to the screen for selection (the first carries the
    // prompt, so it cannot be a slice of the live line).
    prompt_row_buf: [max_prompt_rows][max_line]u8 = undefined,

    const ByteResult = union(enum) { byte: u8, idle, closed };
    // Two logo clicks within this window = double-click, and a SINGLE click is deferred this
    // long before it cycles the accent. That wait — plus the press frame held before it
    // (screen.zig press_ms) — is the whole lag between the click and the colour moving, and
    // both were halved to cut it in two. 250ms is also the interval the browser app's own
    // deferred click uses (ui/popover.js DOUBLE_CLICK_MS), so a double-click stays comfortable.
    const double_click_ms: i64 = 250;

    /// Put `tty_fd` into raw mode (no canonical line editing, no echo, no signal keys).
    pub fn init(tty_fd: std.posix.fd_t) !Editor {
        const orig = try std.posix.tcgetattr(tty_fd);
        var raw = orig;
        raw.lflag.ICANON = false;
        raw.lflag.ECHO = false;
        raw.lflag.ISIG = false; // we handle Ctrl-C / Ctrl-D ourselves
        raw.lflag.IEXTEN = false; // ^V is LNEXT to the tty driver (it would eat our paste chord)
        raw.iflag.IXON = false; // let Ctrl-S reach us (copy) instead of XON/XOFF flow control
        raw.iflag.IXOFF = false;
        try std.posix.tcsetattr(tty_fd, .FLUSH, raw);
        const erase = orig.cc[@intFromEnum(std.posix.V.ERASE)];
        // Bracketed paste (ESC[200~ … ESC[201~): a multi-line paste lands in the buffer as one line.
        _ = std.c.write(std.posix.STDERR_FILENO, "\x1b[?2004h", 8);
        return .{ .fd_in = tty_fd, .fd_out = std.posix.STDERR_FILENO, .orig = orig, .erase = if (erase != 0) erase else 127 };
    }

    pub fn deinit(self: *Editor) void {
        // Hand the terminal back exactly as we found it: no lingering colour/attribute from a
        // half-written accent span, and its own paste mode.
        _ = std.c.write(self.fd_out, "\x1b[0m", 4);
        _ = std.c.write(self.fd_out, "\x1b[?2004l", 8); // disable bracketed paste
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
    /// The input block's wrap geometry: how many columns the FIRST row leaves for the line
    /// (the prompt eats into it) and how wide each continuation row is. Null outside screen
    /// mode, where the terminal's own autowrap runs the show and no width is tracked.
    fn wrapGeom(self: *Editor, prompt: []const u8) ?struct { first: usize, cols: usize } {
        const s = self.screen orelse return null;
        const cols: usize = @max(@as(usize, 8), s.cols);
        return .{ .first = if (cols > prompt.len + 1) cols - prompt.len else 1, .cols = cols };
    }

    fn refresh(self: *Editor, prompt: []const u8, line: []const u8, pos: usize) void {
        // Theme the command word only if it starts with '/'; otherwise nothing in the input.
        const cmd_end: usize = if (line.len != 0 and line[0] == '/')
            (std.mem.indexOfAny(u8, line, " \t") orelse line.len)
        else
            0;
        const s = self.screen orelse return self.refreshFlat(prompt, line, pos, cmd_end);

        // Full-screen: the input WRAPS onto as many rows as it needs (the block grows upward
        // into the output, like Claude Code's), instead of scrolling a one-row window sideways
        // — a long prompt you cannot read back is a prompt you cannot check before sending.
        const g = self.wrapGeom(prompt) orelse return;
        const cols = g.cols;
        const first_avail = g.first;
        const need = wrappedRows(line.len, first_avail, cols);
        const max_rows: usize = s.maxPromptRows();
        s.setPromptRows(@intCast(@min(need, max_rows)));
        // Past the cap the block stops growing and scrolls by whole rows, keeping the row the
        // cursor is on in view — the same rule the one-row window used, one dimension up.
        const cur_row = wrappedRows(pos + 1, first_avail, cols) - 1;
        const shown = @min(need, max_rows);
        const skip = if (cur_row >= shown) cur_row - shown + 1 else 0;

        const top = s.promptRow();
        // What each input row ends up showing, handed to the screen so the mouse can select
        // the line being typed (screen.setPromptText) — the scrollback never sees it.
        var shown_rows: [max_prompt_rows][]const u8 = undefined;
        var shown_n: usize = 0;
        var row: usize = 0;
        while (row < shown) : (row += 1) {
            const idx = row + skip;
            const seg = rowSlice(line, idx, first_avail, cols);
            self.gotoRow(@intCast(top + row));
            if (idx == 0) {
                self.writeAll(logo.accentReal());
                self.writeAll(prompt);
                // The accent covers the command token only.
                const split = @min(cmd_end, seg.len);
                self.writeAll(seg[0..split]);
                self.writeAll(logo.resetSeq());
                self.writeAll(seg[split..]);
                // The stored copy is plain text: the prompt plus what this row shows.
                shown_rows[shown_n] = std.fmt.bufPrint(self.prompt_row_buf[shown_n][0..], "{s}{s}", .{ prompt, seg }) catch seg;
            } else {
                self.writeAll(seg);
                shown_rows[shown_n] = seg;
            }
            if (shown_n + 1 < max_prompt_rows) shown_n += 1;
            self.writeAll("\x1b[K");
        }
        s.setPromptText(shown_rows[0..shown_n]);
        // We just overwrote whatever the screen had painted on these rows — including a live
        // selection wash. Put it back, or a drag over the input would flash and vanish on the
        // very next event (every mouse event ends in a refresh).
        if (s.hasHighlight()) s.paintPromptSelection();
        // Park the cursor where the next keystroke lands.
        const col = if (cur_row == 0) prompt.len + pos else (pos - first_avail) % cols;
        self.gotoRow(@intCast(top + (cur_row - skip)));
        if (col > 0) {
            var fbuf: [16]u8 = undefined;
            self.writeAll(std.fmt.bufPrint(&fbuf, "\x1b[{d}C", .{col}) catch return);
        }
    }

    /// The plain (non-full-screen) redraw: one row, the terminal's own autowrap does the rest.
    fn refreshFlat(self: *Editor, prompt: []const u8, line: []const u8, pos: usize, cmd_end: usize) void {
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

    /// Move to column 1 of an absolute screen row.
    fn gotoRow(self: *Editor, row: u16) void {
        var b: [16]u8 = undefined;
        self.writeAll(std.fmt.bufPrint(&b, "\x1b[{d};1H", .{row}) catch "\r");
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
        if (self.screen) |s| {
            self.clearPromptBlock();
            // Hand the rows back: the submitted line is gone, so a block that grew to fit it
            // must shrink or the output stays squeezed under a band of blank rows.
            s.setPromptRows(1);
        } else {
            self.writeAll("\r\n");
        }
    }

    /// Erase EVERY row the input block owns. Clearing only `promptRow()` leaves the
    /// continuation rows of a wrapped line on screen: they sit below the output body, so
    /// nothing repaints them until the block next changes height — which is why a submitted
    /// two-row prompt used to leave its second row hanging there for the whole command.
    fn clearPromptBlock(self: *Editor) void {
        const s = self.screen orelse return;
        const top = s.promptRow();
        var i: u16 = 0;
        while (i < s.promptRows()) : (i += 1) {
            self.gotoRow(top + i);
            self.writeAll("\x1b[2K");
        }
        self.gotoRow(top);
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

    /// Watch the tty for up to `timeout_ms` while a long command runs (an LLM turn): true when
    /// the user pressed Ctrl-C. Everything else readable is DROPPED — type-ahead during a call
    /// has no line to land in, and a queued Ctrl-C would otherwise arm the exit afterwards, so
    /// a press that arrives mid-call means "cancel this", never "quit later".
    pub fn pollInterrupt(self: *Editor, timeout_ms: i32) bool {
        var seen = false;
        var budget: u8 = 64; // bound the drain: a paste can leave a lot behind
        while (budget > 0) : (budget -= 1) {
            switch (self.pollByte(if (seen) 0 else timeout_ms)) {
                .idle, .closed => break,
                .byte => |b| if (b == 3) {
                    seen = true;
                },
            }
        }
        return seen;
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

    // ── images pasted into the line being typed ───────────────────────────────

    // Make room for a line of output printed mid-edit (a paste's note): wipe the prompt row.
    // The caller repaints it afterwards, exactly as the idle hook does.
    fn clearForOutput(self: *Editor) void {
        if (self.screen != null) return self.clearPromptBlock();
        self.gotoLineStart();
        self.writeAll("\x1b[2K");
    }

    // Ctrl-V: hand the clipboard to the host, which holds the picture and gives back the
    // short name its marker shows. True when the chord was handled here — with no hooks
    // wired the caller falls back to returning `.paste`.
    fn attachClipboard(self: *Editor, prompt: []const u8, buf: []u8, len: *usize, pos: *usize) bool {
        const p = self.pending orelse return false;
        var lb: [max_marker_label]u8 = undefined;
        var tb: [max_line]u8 = undefined;
        // Reading the clipboard shells out and takes a moment: leave the line ON SCREEN for
        // it and erase the row only if the hook actually says something (logo's one-shot
        // pre-print hook). Blanking it up front is what made the input blink on every paste.
        self.armLineClear();
        const what = p.paste(p.ctx, buf[0..len.*], &lb, &tb);
        logo.disarmPrePrint();
        switch (what) {
            // A picture: it rides the line as a marker until Enter loads it.
            .image => |n| self.insertMarker(prompt, buf, len, pos, p.count(p.ctx), lb[0..n]),
            // Plain text on the clipboard is a plain paste — Ctrl-V is the paste key, and
            // refusing to type what was copied because it is not a picture helps nobody.
            .text => |n| self.insertText(prompt, buf, len, pos, sanitizeInline(tb[0..n])),
            .none => self.refresh(prompt, buf[0..len.*], pos.*),
        }
        return true;
    }

    // Copy the live mouse selection (full-screen mode) to the clipboard. False when there is
    // none, so the caller can fall through to whatever the key otherwise means.
    fn copySelection(self: *Editor) bool {
        const s = self.screen orelse return false;
        if (!s.hasSelection()) return false;
        const text = s.takeSelection();
        if (text.len != 0) {
            if (self.copy_text_cb) |cb| cb(self.logo_ctx.?, text);
        }
        return true;
    }

    // Erase the prompt row, as the one-shot pre-print hook: the caller repaints right after.
    fn clearLineTrampoline(raw: *anyopaque) void {
        const self: *Editor = @ptrCast(@alignCast(raw));
        self.clearForOutput();
    }

    fn armLineClear(self: *Editor) void {
        logo.armPrePrint(clearLineTrampoline, self);
    }

    // Ctrl-Z: take back the last image pasted into THIS line, marker and all. False when the
    // line holds none, so the chord falls through to the session's own `/unpaste`.
    fn dropLastMarker(self: *Editor, prompt: []const u8, buf: []u8, len: *usize, pos: *usize) bool {
        const p = self.pending orelse return false;
        if (p.count(p.ctx) == 0) return false;
        var start: usize = 0;
        var end: usize = 0;
        var i: usize = 0;
        while (i < len.*) {
            if (markerEnd(buf[0..len.*], i)) |e| {
                start = i;
                end = e;
                i = e;
            } else i += 1;
        }
        if (end == 0) { // markers all gone already — drop what they stood for and carry on
            p.keep(p.ctx, &.{});
            return true;
        }
        if (end < len.* and buf[end] == ' ') end += 1; // take the marker's own spacer with it
        std.mem.copyForwards(u8, buf[start..], buf[end..len.*]);
        len.* -= end - start;
        pos.* = if (pos.* >= end) pos.* - (end - start) else @min(pos.*, start);
        self.syncPending(buf[0..len.*]);
        self.refresh(prompt, buf[0..len.*], pos.*);
        return true;
    }

    // The line is being abandoned (Ctrl-C / Ctrl-D / Ctrl-U / a closed tty): whatever was
    // pasted into it goes too — nothing was ever loaded.
    fn abortPending(self: *Editor) void {
        const p = self.pending orelse return;
        if (p.count(p.ctx) != 0) p.keep(p.ctx, &.{});
    }

    // Reconcile the host's images with the markers the line actually still holds: the
    // surviving ones are kept in the order they now read (and renumbered in place, so the
    // first marker is always #1); the rest are dropped. Cheap, and a no-op with none pending.
    fn syncPending(self: *Editor, line: []u8) void {
        const p = self.pending orelse return;
        if (p.count(p.ctx) == 0) return;
        var kept: [max_pending_images]usize = undefined;
        var n: usize = 0;
        var i: usize = 0;
        while (i < line.len) {
            const end = markerEnd(line, i) orelse {
                i += 1;
                continue;
            };
            const idx: usize = line[i + marker_open.len] - '1';
            if (n < kept.len and std.mem.indexOfScalar(usize, kept[0..n], idx) == null) {
                kept[n] = idx;
                n += 1;
                line[i + marker_open.len] = '0' + @as(u8, @intCast(n));
            }
            i = end;
        }
        p.keep(p.ctx, kept[0..n]);
    }

    // Insert "[Image #N <label>]" at the cursor, spaced off the surrounding words so the line
    // still reads as the sentence being written.
    fn insertMarker(self: *Editor, prompt: []const u8, buf: []u8, len: *usize, pos: *usize, n: usize, label: []const u8) void {
        var m: [max_marker_label + 16]u8 = undefined;
        const lead: []const u8 = if (pos.* != 0 and buf[pos.* - 1] != ' ') " " else "";
        const text = std.fmt.bufPrint(&m, "{s}{s}{d} {s}] ", .{ lead, marker_open, n, label }) catch return;
        self.insertText(prompt, buf, len, pos, text);
    }

    // Insert `text` at the cursor (silently ignored when the line has no room left).
    fn insertText(self: *Editor, prompt: []const u8, buf: []u8, len: *usize, pos: *usize, text: []const u8) void {
        if (len.* + text.len <= buf.len) {
            if (pos.* < len.*) std.mem.copyBackwards(u8, buf[pos.* + text.len .. len.* + text.len], buf[pos.*..len.*]);
            @memcpy(buf[pos.*..][0..text.len], text);
            len.* += text.len;
            pos.* += text.len;
        }
        self.refresh(prompt, buf[0..len.*], pos.*);
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
                        if (len == 0) {
                            self.abortPending();
                            return .eof;
                        }
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
                3 => { // Ctrl-C: with text selected it COPIES that — what every desktop does,
                    // and the one thing the mouse selection was for. Otherwise it confirms exit.
                    if (self.copySelection()) {
                        armed.* = false; // a copy is an action of its own: it disarms the exit
                        self.refresh(prompt, buf[0..len], pos);
                        continue;
                    }
                    self.abortPending();
                    self.endPromptLine();
                    return .interrupt;
                },
                4 => { // Ctrl-D (EOF): leave the console
                    self.abortPending();
                    self.endPromptLine();
                    return .eof;
                },
                22 => { // Ctrl-V: attach the clipboard's image to the line being typed
                    // Plain Ctrl-V, not just the Meta chord: a terminal that keeps Option for
                    // itself (VS Code's, by default) never delivers ESC-Ctrl-V, and ⌘V belongs
                    // to the terminal — it pastes TEXT and can't hand an image to us at all.
                    if (self.attachClipboard(prompt, buf, &len, &pos)) continue;
                    self.endPromptLine();
                    return .paste;
                },
                26 => { // Ctrl-Z: take back the last image added
                    // Free to bind: ISIG is off in raw mode, so this never suspended us. An
                    // image pasted into THIS line goes first; with none, the session's own.
                    if (self.dropLastMarker(prompt, buf, &len, &pos)) continue;
                    self.endPromptLine();
                    return .unpaste;
                },
                21 => { // Ctrl-U: clear the line — and anything pasted into it
                    len = 0;
                    pos = 0;
                    self.abortPending();
                    self.refresh(prompt, buf[0..0], 0);
                },
                19 => if (self.screen != null) { // Ctrl-S: copy the selection — or the typed line
                    // The prompt row is outside the selectable body (drag-selection covers the
                    // scrollback), so with nothing highlighted this copies what you are typing:
                    // the one piece of text the mouse cannot reach.
                    if (!self.copySelection() and len != 0) {
                        if (self.copy_text_cb) |cb| cb(self.logo_ctx.?, buf[0..len]);
                    }
                    self.refresh(prompt, buf[0..len], pos);
                },
                23 => self.deleteWordBack(prompt, buf, &len, &pos), // Ctrl-W: delete the word before the cursor
                11 => if (pos < len) { // Ctrl-K: kill from the cursor to end of line
                    len = pos;
                    self.syncPending(buf[0..len]);
                    self.refresh(prompt, buf[0..len], pos);
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
                127, 8 => { // Backspace — and, on the byte this tty does NOT send for it,
                    // Ctrl-Backspace, which deletes the word (what VS Code, Windows and most
                    // Linux terminals send as 0x08).
                    if (ch == self.erase or ch == 127) self.backspace(prompt, buf, &len, &pos)
                    else self.deleteWordBack(prompt, buf, &len, &pos);
                },
                27 => { // ESC: a nav/mouse sequence, or an Alt/Meta-modified chord
                    const nxt = self.readByte() orelse continue; // lone ESC: ignore
                    switch (nxt) {
                        3 => { // Ctrl-Alt-C: copy the image to the clipboard
                            self.abortPending();
                            self.endPromptLine();
                            return .copy;
                        },
                        22 => { // Ctrl-Alt-V: same as Ctrl-V, for terminals that send Meta
                            if (self.attachClipboard(prompt, buf, &len, &pos)) continue;
                            self.endPromptLine();
                            return .paste;
                        },
                        26 => { // Ctrl-Alt-Z: take back the last image added
                            if (self.dropLastMarker(prompt, buf, &len, &pos)) continue;
                            self.endPromptLine();
                            return .unpaste;
                        },
                        // Meta (Alt/Option) word chords — emacs bindings, also Option+←/→.
                        'b' => { // Alt-b: word left
                            pos = wordLeft(buf[0..len], pos);
                            self.refresh(prompt, buf[0..len], pos);
                        },
                        'f' => { // Alt-f: word right
                            pos = wordRight(buf[0..len], pos);
                            self.refresh(prompt, buf[0..len], pos);
                        },
                        'd' => self.deleteWordFwd(prompt, buf, &len, &pos), // Alt-d: delete word forward
                        8, 127 => self.deleteWordBack(prompt, buf, &len, &pos), // Alt-Backspace: delete word back
                        // Meta-prefixed sequence (ESC ESC [ D = Option+Left under "Option as Meta"):
                        // the leading ESC is the modifier, so parse the inner CSI and force word-motion.
                        27 => {
                            const intro = self.readByte() orelse continue;
                            if (intro == '[' or intro == 'O') {
                                var params: [16]u8 = undefined;
                                const r = self.collectCsi(&params);
                                self.csi(params[0..r.np], r.final, true, prompt, buf, &len, &pos, hist, &hidx, &stash, &stash_len);
                            }
                        },
                        '[' => {
                            const b2 = self.readByte() orelse continue;
                            if (b2 == '<') { // SGR mouse report
                                self.handleMouse(prompt, buf, &len, &pos, &click_pending, &click_at);
                            } else {
                                // Collect the CSI params so a modified key like Alt-Left
                                // (ESC [ 1 ; 3 D) parses as one sequence, not "1;3D" in the buffer.
                                var params: [16]u8 = undefined;
                                const r = self.collectCsi2(b2, &params);
                                if (r.final == '~' and std.mem.eql(u8, params[0..r.np], "200")) // bracketed paste start
                                    self.readPaste(prompt, buf, &len, &pos)
                                else
                                    self.csi(params[0..r.np], r.final, false, prompt, buf, &len, &pos, hist, &hidx, &stash, &stash_len);
                            }
                        },
                        'O' => { // application cursor keys: ESC O <final>, no parameters
                            const code = self.readByte() orelse continue;
                            self.csi(&.{}, code, false, prompt, buf, &len, &pos, hist, &hidx, &stash, &stash_len);
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

    const CsiResult = struct { np: usize, final: u8 };

    // Read CSI parameter bytes (digits and ';') into `out`, returning the final non-param byte.
    fn collectCsi(self: *Editor, out: []u8) CsiResult {
        return self.collectCsi2(self.readByte() orelse 0, out);
    }
    // Same, but `first` is a parameter byte the caller already read (e.g. while sniffing for '<').
    fn collectCsi2(self: *Editor, first: u8, out: []u8) CsiResult {
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

    // Act on a collected CSI nav sequence (`ESC [ params final`, mouse excluded). A modifier param
    // > 1 or a Meta ESC prefix (`force_word`) turns a plain arrow into a word jump. Application-
    // cursor keys (`ESC O <final>`) arrive with empty params.
    fn csi(self: *Editor, params: []const u8, final: u8, force_word: bool, prompt: []const u8, buf: []u8, len: *usize, pos: *usize, hist: *History, hidx: *usize, stash: []u8, stash_len: *usize) void {
        var it = std.mem.splitScalar(u8, params, ';');
        const code: u32 = std.fmt.parseInt(u32, it.next() orelse "", 10) catch 0;
        const mod: u32 = std.fmt.parseInt(u32, it.next() orelse "", 10) catch 0;
        const word = force_word or mod > 1; // any modifier on an arrow = move by word
        switch (final) {
            // Up/Down first move BETWEEN the rows of a wrapped line — a two-row prompt you
            // cannot walk back into is a prompt you cannot fix. Only from the top row (Up) or
            // the bottom one (Down) do they mean the usual previous/next command.
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
            // ESC [ <code> ; <mods> u — how xterm's modifyOtherKeys and the kitty protocol
            // report a MODIFIED key. Backspace (127, or 8 where that is the erase byte) with
            // any modifier is the "delete the word" chord; bare, it is one character.
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

    // One Backspace. On an image marker it takes the WHOLE picture back, not one byte of its
    // name — the marker is one thing on screen, so it is one thing to delete.
    fn backspace(self: *Editor, prompt: []const u8, buf: []u8, len: *usize, pos: *usize) void {
        if (pos.* == 0) return;
        if (markerBefore(buf[0..len.*], pos.*)) |start| {
            const gone = pos.* - start;
            std.mem.copyForwards(u8, buf[start .. len.* - gone], buf[pos.*..len.*]);
            pos.* = start;
            len.* -= gone;
        } else {
            std.mem.copyForwards(u8, buf[pos.* - 1 .. len.* - 1], buf[pos.*..len.*]);
            pos.* -= 1;
            len.* -= 1;
        }
        self.syncPending(buf[0..len.*]);
        self.refresh(prompt, buf[0..len.*], pos.*);
    }

    // Delete from the start of the word before the cursor up to the cursor (Ctrl-W / Alt-Backspace).
    fn deleteWordBack(self: *Editor, prompt: []const u8, buf: []u8, len: *usize, pos: *usize) void {
        const start = wordLeft(buf[0..len.*], pos.*);
        if (start == pos.*) return;
        const removed = pos.* - start;
        std.mem.copyForwards(u8, buf[start .. len.* - removed], buf[pos.*..len.*]);
        len.* -= removed;
        pos.* = start;
        self.syncPending(buf[0..len.*]);
        self.refresh(prompt, buf[0..len.*], pos.*);
    }

    // Delete from the cursor to the end of the word ahead of it (Alt-d / modified Delete).
    fn deleteWordFwd(self: *Editor, prompt: []const u8, buf: []u8, len: *usize, pos: *usize) void {
        const end = wordRight(buf[0..len.*], pos.*);
        if (end == pos.*) return;
        const removed = end - pos.*;
        std.mem.copyForwards(u8, buf[pos.* .. len.* - removed], buf[end..len.*]);
        len.* -= removed;
        self.syncPending(buf[0..len.*]);
        self.refresh(prompt, buf[0..len.*], pos.*);
    }

    // Read a bracketed paste (`ESC [ 200 ~` consumed) up to the `ESC [ 201 ~` terminator and insert
    // it at the cursor. Control bytes (notably newlines) become spaces, so it lands as one line.
    fn readPaste(self: *Editor, prompt: []const u8, buf: []u8, len: *usize, pos: *usize) void {
        const start = pos.*;
        while (true) {
            const b = self.readByte() orelse break;
            if (b == 27) { // an escape inside the paste — the only one we expect is the terminator
                if ((self.readByte() orelse break) != '[') continue; // unknown → drop the introducer
                var pr: [8]u8 = undefined;
                var pn: usize = 0;
                var bb = self.readByte() orelse break;
                while ((bb >= '0' and bb <= '9') or bb == ';') {
                    if (pn < pr.len) {
                        pr[pn] = bb;
                        pn += 1;
                    }
                    bb = self.readByte() orelse break;
                }
                if (bb == '~' and std.mem.eql(u8, pr[0..pn], "201")) break; // end of paste
                continue; // some other CSI inside the paste — ignore it
            }
            const c: u8 = if (b < 0x20 or b == 0x7f) ' ' else b; // newlines/controls → space
            if (len.* >= buf.len) continue; // buffer full — drop the rest of the paste
            if (pos.* < len.*) std.mem.copyBackwards(u8, buf[pos.* + 1 .. len.* + 1], buf[pos.*..len.*]);
            buf[pos.*] = c;
            pos.* += 1;
            len.* += 1;
        }
        // A paste that delivered NOTHING is what a terminal does when the clipboard holds only
        // an image: its paste event carries text, and there is none. Take the picture off the
        // clipboard ourselves — pressing ⌘V/Ctrl-Shift-V with an image copied means exactly
        // what Ctrl-V means here.
        if (pos.* == start and self.pending != null) {
            _ = self.attachClipboard(prompt, buf, len, pos);
            return;
        }
        self.attachPastedPath(prompt, buf, len, pos, start);
        self.syncPending(buf[0..len.*]);
        self.refresh(prompt, buf[0..len.*], pos.*);
    }

    // A ⌘V that pasted nothing but the path of an image FILE becomes a marker instead of the
    // raw text: the terminal cannot hand over the picture itself, only its name. The host
    // decides (it knows the command being typed, and whether the file is really an image) —
    // when it declines, the pasted text simply stays.
    fn attachPastedPath(self: *Editor, prompt: []const u8, buf: []u8, len: *usize, pos: *usize, start: usize) void {
        const p = self.pending orelse return;
        if (pos.* <= start) return;
        var lb: [max_marker_label]u8 = undefined;
        self.armLineClear(); // the hook may print; erase the row only then (see attachClipboard)
        const n = p.addPath(p.ctx, buf[start..pos.*], buf[0..start], &lb) orelse {
            logo.disarmPrePrint();
            return;
        };
        logo.disarmPrePrint();
        std.mem.copyForwards(u8, buf[start..], buf[pos.*..len.*]); // the path is now the picture
        len.* -= pos.* - start;
        pos.* = start;
        self.insertMarker(prompt, buf, len, pos, p.count(p.ctx), lb[0..n]);
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
            s.selDrag(ev.col, ev.row); // extend the visual text selection
            self.refresh(prompt, buf[0..len.*], pos.*);
        } else if (ev.isRelease()) {
            if (s.selActive()) { // finished a drag → settle the highlight (visual only, no copy)
                s.selEnd();
                self.refresh(prompt, buf[0..len.*], pos.*);
            }
        } else if (ev.isLeftPress()) {
            if (s.inHeader(ev.row)) { // logo click: deferred single (cycle) / double (custom)
                s.pressLogo(); // immediate feedback: the logo shrinks and springs back
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
            } else { // press on an output row → begin a visual text selection
                s.selStart(ev.col, ev.row);
                self.refresh(prompt, buf[0..len.*], pos.*);
            }
        }
    }

    // Up (older) / Down (newer) through history, stashing the fresh line on first Up.
    /// Move the cursor one wrapped row up/down inside a multi-row input. False when there is
    /// no such row (or no screen geometry at all), leaving Up/Down to history recall.
    fn rowStep(self: *Editor, prompt: []const u8, line: []const u8, pos: *usize, up: bool) bool {
        const g = self.wrapGeom(prompt) orelse return false;
        const to = rowMove(line.len, pos.*, prompt.len, g.first, g.cols, up) orelse return false;
        pos.* = to;
        return true;
    }

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
        self.syncPending(buf[0..len.*]); // a recalled line carries no markers: images pasted into the abandoned one go
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

/// The end offset (exclusive) of the `[Image #N …]` marker starting at `i`, or null when no
/// marker starts there. The index is one digit, so a marker is always removed and renumbered
/// as a whole.
pub fn markerEnd(line: []const u8, i: usize) ?usize {
    if (i + marker_open.len + 2 > line.len) return null;
    if (!std.mem.eql(u8, line[i..][0..marker_open.len], marker_open)) return null;
    const d = line[i + marker_open.len];
    if (d < '1' or d > '0' + max_pending_images) return null;
    const rest = line[i + marker_open.len + 1 ..];
    const close = std.mem.indexOfScalar(u8, rest, ']') orelse return null;
    return i + marker_open.len + 1 + close + 1;
}

/// The start of the marker ENDING at `pos`, or null when the cursor isn't right behind one.
pub fn markerBefore(line: []const u8, pos: usize) ?usize {
    if (pos == 0 or pos > line.len or line[pos - 1] != ']') return null;
    var i = pos - 1;
    while (true) : (i -= 1) {
        if (line[i] == '[') {
            if (markerEnd(line, i)) |e| {
                if (e == pos) return i;
            }
            return null;
        }
        if (i == 0) return null;
    }
}

/// `line` with every image marker taken out (and the gap it left closed), written into `out`
/// — the command the console actually dispatches once the pictures have been lifted off it.
/// Returns `line` itself, untouched, when it carries no markers.
pub fn stripMarkers(out: []u8, line: []const u8) []const u8 {
    if (std.mem.indexOf(u8, line, marker_open) == null) return line;
    var n: usize = 0;
    var i: usize = 0;
    while (i < line.len) {
        if (markerEnd(line, i)) |e| {
            i = e;
            continue;
        }
        // Never leave a doubled (or leading) space where a marker stood.
        if (!(line[i] == ' ' and (n == 0 or out[n - 1] == ' '))) {
            out[n] = line[i];
            n += 1;
        }
        i += 1;
    }
    return std.mem.trimEnd(u8, out[0..n], " ");
}

/// Pasted text as ONE editable line: control bytes (newlines above all) become spaces, so it
/// lands the way a bracketed paste does and nothing runs until Enter.
fn sanitizeInline(text: []u8) []const u8 {
    for (text) |*c| {
        if (c.* < 0x20 or c.* == 0x7f) c.* = ' ';
    }
    return std.mem.trimEnd(u8, text, " ");
}

// A word character for cursor motion: anything non-whitespace. Word jumps skip a run of
// separators, then the run of word characters (bash/emacs-style).
fn isWordChar(c: u8) bool {
    return c > ' ' and c != 0x7f;
}

/// One word to the LEFT of `pos`: back over separators, then over the word. 0 at line start.
fn wordLeft(line: []const u8, pos: usize) usize {
    var p = @min(pos, line.len);
    while (p > 0 and !isWordChar(line[p - 1])) p -= 1;
    while (p > 0 and isWordChar(line[p - 1])) p -= 1;
    return p;
}

/// One word to the RIGHT of `pos`: forward over separators, then over the word. `line.len` at end.
fn wordRight(line: []const u8, pos: usize) usize {
    var p = @min(pos, line.len);
    while (p < line.len and !isWordChar(line[p])) p += 1;
    while (p < line.len and isWordChar(line[p])) p += 1;
    return p;
}

const testing = std.testing;

test "image markers: found, deleted whole, and stripped off the submitted line" {
    const line = "look [Image #1 shot.png] at this";
    try testing.expectEqual(@as(?usize, 24), markerEnd(line, 5));
    try testing.expectEqual(@as(?usize, null), markerEnd(line, 0)); // not a marker start
    try testing.expectEqual(@as(?usize, null), markerEnd("[Image #9 x]", 0)); // index out of range
    try testing.expectEqual(@as(?usize, 5), markerBefore(line, 24)); // cursor right behind it
    try testing.expectEqual(@as(?usize, null), markerBefore(line, 23));

    var out: [max_line]u8 = undefined;
    try testing.expectEqualStrings("look at this", stripMarkers(&out, line));
    try testing.expectEqualStrings("/prompt describe", stripMarkers(&out, "/prompt [Image #1 a.png] describe"));
    try testing.expectEqualStrings("/upload", stripMarkers(&out, "/upload [Image #1 a.png] "));
    try testing.expectEqualStrings("", stripMarkers(&out, "[Image #1 a.png]"));
    // A line with no markers is handed back byte-for-byte (spacing included).
    try testing.expectEqualStrings("/crop  x1=1", stripMarkers(&out, "/crop  x1=1"));
}

// A stand-in host for the pending-image hooks: counts what it holds, no clipboard involved.
const MockPending = struct {
    held: usize = 0,
    last_kept: usize = 0,
    give_text: ?[]const u8 = null, // set to answer a Ctrl-V with clipboard TEXT instead

    fn hooks(self: *MockPending) PendingImages {
        return .{ .ctx = self, .paste = add, .addPath = addPath, .keep = keep, .count = count };
    }
    fn add(ctx: *anyopaque, before: []const u8, label: []u8, text: []u8) PasteResult {
        const self: *MockPending = @ptrCast(@alignCast(ctx));
        if (self.give_text) |t| {
            @memcpy(text[0..t.len], t);
            return .{ .text = t.len };
        }
        if (self.held >= max_pending_images or std.mem.startsWith(u8, before, "/upload")) return .none;
        self.held += 1;
        const name = "shot.png";
        @memcpy(label[0..name.len], name);
        return .{ .image = name.len };
    }
    fn addPath(_: *anyopaque, _: []const u8, _: []const u8, _: []u8) ?usize {
        return null; // this host never claims a pasted path
    }
    fn keep(ctx: *anyopaque, kept: []const usize) void {
        const self: *MockPending = @ptrCast(@alignCast(ctx));
        self.held = kept.len;
        self.last_kept = kept.len;
    }
    fn count(ctx: *anyopaque) usize {
        const self: *MockPending = @ptrCast(@alignCast(ctx));
        return self.held;
    }
};

test "Ctrl-V drops an image marker into the line; Backspace over it takes the picture back" {
    const in = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(in[0]);
    const out = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(out[0]);
    defer _ = std.c.close(out[1]);

    var mock = MockPending{};
    var ed = Editor{ .fd_in = in[0], .fd_out = out[1], .orig = std.mem.zeroes(std.posix.termios) };
    ed.pending = mock.hooks();
    var hist = History{ .gpa = testing.allocator };
    defer hist.deinit();
    var buf: [max_line]u8 = undefined;
    var armed = false;

    // "hi" Ctrl-V Enter — the chord no longer ends the line, it attaches to it.
    const typed = [_]u8{ 'h', 'i', 22, '\r' };
    _ = std.c.write(in[1], &typed, typed.len);
    const first = ed.readLine("> ", &buf, &hist, &.{}, &armed, "");
    try testing.expectEqualStrings("hi [Image #1 shot.png] ", buf[0..first.line]);
    try testing.expectEqual(@as(usize, 1), mock.held);

    // Backspace eats the trailing space, a second one the WHOLE marker — and the host is
    // told, so the picture behind it is dropped rather than orphaned.
    mock.held = 1;
    const del = [_]u8{ 22, 127, 127, '\r' };
    _ = std.c.write(in[1], &del, del.len);
    _ = std.c.close(in[1]);
    const second = ed.readLine("> ", &buf, &hist, &.{}, &armed, "");
    try testing.expectEqualStrings("", buf[0..second.line]);
    try testing.expectEqual(@as(usize, 0), mock.held);
}

test "with images pending, Ctrl-Z takes the last one back instead of leaving the line" {
    const in = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(in[0]);
    const out = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(out[0]);
    defer _ = std.c.close(out[1]);

    var mock = MockPending{};
    var ed = Editor{ .fd_in = in[0], .fd_out = out[1], .orig = std.mem.zeroes(std.posix.termios) };
    ed.pending = mock.hooks();
    var hist = History{ .gpa = testing.allocator };
    defer hist.deinit();
    var buf: [max_line]u8 = undefined;
    var armed = false;

    const keys = [_]u8{ 22, 22, 26, '\r' }; // two images, then un-paste one
    _ = std.c.write(in[1], &keys, keys.len);
    const res = ed.readLine("> ", &buf, &hist, &.{}, &armed, "");
    try testing.expectEqualStrings("[Image #1 shot.png] ", buf[0..res.line]);
    try testing.expectEqual(@as(usize, 1), mock.held);

    // With none left — the console drains the line's images as it runs it — Ctrl-Z is the
    // session's own `/unpaste` again.
    mock.held = 0;
    const z = [_]u8{26};
    _ = std.c.write(in[1], &z, z.len);
    _ = std.c.close(in[1]);
    try testing.expect(ed.readLine("> ", &buf, &hist, &.{}, &armed, "") == .unpaste);
}

test "a fourth image is refused rather than silently dropped" {
    const in = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(in[0]);
    const out = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(out[0]);
    defer _ = std.c.close(out[1]);
    var swallowed: u8 = 0;
    logo.setSink(struct {
        fn sink(_: *anyopaque, _: []const u8) void {}
    }.sink, &swallowed);
    defer logo.clearSink();

    var mock = MockPending{};
    var ed = Editor{ .fd_in = in[0], .fd_out = out[1], .orig = std.mem.zeroes(std.posix.termios) };
    ed.pending = mock.hooks();
    var hist = History{ .gpa = testing.allocator };
    defer hist.deinit();
    var buf: [max_line]u8 = undefined;
    var armed = false;

    const keys = [_]u8{ 22, 22, 22, 22, '\r' };
    _ = std.c.write(in[1], &keys, keys.len);
    _ = std.c.close(in[1]);
    const res = ed.readLine("> ", &buf, &hist, &.{}, &armed, "");
    try testing.expectEqual(@as(usize, max_pending_images), mock.held);
    try testing.expectEqual(@as(usize, 3), std.mem.count(u8, buf[0..res.line], "[Image #"));
}

test "word-delete chords: every encoding a terminal sends for a modified Backspace" {
    // 0x08 (Ctrl-Backspace on VS Code/Windows/Linux), ESC DEL (Alt-Backspace), the CSI-u form
    // modern terminals report modified keys with, and plain Ctrl-W all kill the word; plain
    // Backspace and a BARE CSI-u still take one character.
    const cases = [_]struct { keys: []const u8, want: []const u8 }{
        .{ .keys = "\x7f", .want = "/crop one two thre" },
        .{ .keys = "\x08", .want = "/crop one two " },
        .{ .keys = "\x1b\x7f", .want = "/crop one two " },
        .{ .keys = "\x1b[127;5u", .want = "/crop one two " },
        .{ .keys = "\x1b[127u", .want = "/crop one two thre" },
        .{ .keys = "\x17", .want = "/crop one two " },
    };
    for (cases) |c| {
        const in = try std.Io.Threaded.pipe2(.{});
        defer _ = std.c.close(in[0]);
        const out = try std.Io.Threaded.pipe2(.{});
        defer _ = std.c.close(out[0]);
        defer _ = std.c.close(out[1]);
        var ed = Editor{ .fd_in = in[0], .fd_out = out[1], .orig = std.mem.zeroes(std.posix.termios) };
        var hist = History{ .gpa = testing.allocator };
        defer hist.deinit();
        var buf: [max_line]u8 = undefined;
        var armed = false;

        const typed = "/crop one two three";
        _ = std.c.write(in[1], typed.ptr, typed.len);
        _ = std.c.write(in[1], c.keys.ptr, c.keys.len);
        _ = std.c.write(in[1], "\r", 1);
        _ = std.c.close(in[1]);
        const res = ed.readLine("> ", &buf, &hist, &.{}, &armed, "");
        try testing.expectEqualStrings(c.want, buf[0..res.line]);
    }
}

test "a tty that erases with 0x08 keeps it as a plain backspace" {
    // The one terminal family where 0x08 IS the erase key: it must not eat a whole word there.
    const in = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(in[0]);
    const out = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(out[0]);
    defer _ = std.c.close(out[1]);
    var ed = Editor{ .fd_in = in[0], .fd_out = out[1], .orig = std.mem.zeroes(std.posix.termios), .erase = 8 };
    var hist = History{ .gpa = testing.allocator };
    defer hist.deinit();
    var buf: [max_line]u8 = undefined;
    var armed = false;

    const keys = "/crop one two three\x08\r";
    _ = std.c.write(in[1], keys.ptr, keys.len);
    _ = std.c.close(in[1]);
    const res = ed.readLine("> ", &buf, &hist, &.{}, &armed, "");
    try testing.expectEqualStrings("/crop one two thre", buf[0..res.line]);
}

test "a paste that delivered nothing takes the image off the clipboard instead" {
    // ⌘V with only an image copied: the terminal's paste event carries text, and there is
    // none — so the empty bracketed paste is the signal to go and read the picture.
    const in = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(in[0]);
    const out = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(out[0]);
    defer _ = std.c.close(out[1]);

    var mock = MockPending{};
    var ed = Editor{ .fd_in = in[0], .fd_out = out[1], .orig = std.mem.zeroes(std.posix.termios) };
    ed.pending = mock.hooks();
    var hist = History{ .gpa = testing.allocator };
    defer hist.deinit();
    var buf: [max_line]u8 = undefined;
    var armed = false;

    const keys = "ask \x1b[200~\x1b[201~\r"; // an empty bracketed paste mid-line
    _ = std.c.write(in[1], keys.ptr, keys.len);
    _ = std.c.close(in[1]);
    const res = ed.readLine("> ", &buf, &hist, &.{}, &armed, "");
    try testing.expectEqualStrings("ask [Image #1 shot.png] ", buf[0..res.line]);
    try testing.expectEqual(@as(usize, 1), mock.held);
}


test "Ctrl-V types the clipboard's TEXT when it holds no picture" {
    const in = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(in[0]);
    const out = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(out[0]);
    defer _ = std.c.close(out[1]);

    var mock = MockPending{ .give_text = "pasted words" };
    var ed = Editor{ .fd_in = in[0], .fd_out = out[1], .orig = std.mem.zeroes(std.posix.termios) };
    ed.pending = mock.hooks();
    var hist = History{ .gpa = testing.allocator };
    defer hist.deinit();
    var buf: [max_line]u8 = undefined;
    var armed = false;

    const keys = "/prompt say \x16\r";
    _ = std.c.write(in[1], keys.ptr, keys.len);
    _ = std.c.close(in[1]);
    const res = ed.readLine("> ", &buf, &hist, &.{}, &armed, "");
    try testing.expectEqualStrings("/prompt say pasted words", buf[0..res.line]);
    try testing.expectEqual(@as(usize, 0), mock.held); // text is typed, not held as an image
}

test "Ctrl-C copies a live selection; with none it still confirms the exit" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const in = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(in[0]);
    const out = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(out[0]);
    defer _ = std.c.close(out[1]);

    var scr = screen_mod.Screen{ .gpa = a, .io = threaded.io(), .fd = out[1], .rows = 20, .cols = 40 };
    defer scr.freeAllForTest();
    const Copied = struct {
        var count: usize = 0;
        fn sink(_: *anyopaque, _: []const u8) void {
            count += 1;
        }
    };
    Copied.count = 0;
    var ctx: u8 = 0;
    var ed = Editor{ .fd_in = in[0], .fd_out = out[1], .orig = std.mem.zeroes(std.posix.termios), .screen = &scr };
    ed.copy_text_cb = Copied.sink;
    ed.logo_ctx = &ctx;
    var hist = History{ .gpa = testing.allocator };
    defer hist.deinit();
    var buf: [max_line]u8 = undefined;
    var armed = false;

    ed.refresh("> ", "hello world", 11); // the prompt row has to exist before a drag can cover it
    scr.selectForTest(scr.promptRow(), 3, scr.promptRow(), 7); // mouse columns are 1-based
    const keys = [_]u8{ 3, 3 }; // the first press copies, the second (no selection left) exits
    _ = std.c.write(in[1], &keys, keys.len);
    _ = std.c.close(in[1]);
    try testing.expect(ed.readLine("> ", &buf, &hist, &.{}, &armed, "") == .interrupt);
    try testing.expectEqual(@as(usize, 1), Copied.count); // copied once, exited once
}

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

test "wordLeft/wordRight: jump over separator runs then the word" {
    const s = "/crop 10 20 to end";
    //         0123456789...
    try testing.expectEqual(@as(usize, 12), wordLeft(s, 14)); // inside "to" → start of "to"
    try testing.expectEqual(@as(usize, 9), wordLeft(s, 11)); // start of "20"
    try testing.expectEqual(@as(usize, 0), wordLeft(s, 5)); // from the space back to line start
    try testing.expectEqual(@as(usize, 0), wordLeft(s, 0)); // already home

    try testing.expectEqual(@as(usize, 5), wordRight(s, 0)); // over "/crop" to the space's end... "/crop"
    try testing.expectEqual(@as(usize, 8), wordRight(s, 5)); // over " 10"
    try testing.expectEqual(@as(usize, s.len), wordRight(s, 15)); // "end" → end of line
    try testing.expectEqual(@as(usize, s.len), wordRight(s, s.len)); // already at end
}

test "plain Ctrl-V / Ctrl-Z resolve to the paste / un-paste actions" {
    // Driven over a pipe rather than a tty: readLine only reads bytes, and the actions under
    // test need no screen. (Ctrl-V is the binding Claude Code's CLI uses for the same job —
    // terminals deliver it untouched, unlike the Option/Meta chords.)
    const in = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(in[0]);
    const out = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(out[0]);
    defer _ = std.c.close(out[1]);

    var ed = Editor{ .fd_in = in[0], .fd_out = out[1], .orig = std.mem.zeroes(std.posix.termios) };
    var hist = History{ .gpa = testing.allocator };
    defer hist.deinit();
    var buf: [max_line]u8 = undefined;
    var armed = false;

    const keys = [_]u8{ 22, 26 }; // Ctrl-V then Ctrl-Z
    _ = std.c.write(in[1], &keys, keys.len); // libc write, like refresh() (no std.posix.write)
    _ = std.c.close(in[1]);

    try testing.expect(ed.readLine("> ", &buf, &hist, &.{}, &armed, "") == .paste);
    try testing.expect(ed.readLine("> ", &buf, &hist, &.{}, &armed, "") == .unpaste);
    // The stream ends there: a closed input still leaves the console, as before.
    try testing.expect(ed.readLine("> ", &buf, &hist, &.{}, &armed, "") == .eof);
}

test "pollInterrupt: a Ctrl-C is reported, other type-ahead is dropped" {
    const in = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(in[0]);
    const out = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(out[0]);
    defer _ = std.c.close(out[1]);
    var ed = Editor{ .fd_in = in[0], .fd_out = out[1], .orig = std.mem.zeroes(std.posix.termios) };

    const typed = [_]u8{ 'a', 'b', '\n' }; // keystrokes during a call have no line to land in
    _ = std.c.write(in[1], &typed, typed.len);
    try testing.expect(!ed.pollInterrupt(0));

    const press = [_]u8{ 'x', 3 }; // …and a Ctrl-C among them still reads as "cancel this"
    _ = std.c.write(in[1], &press, press.len);
    try testing.expect(ed.pollInterrupt(0));

    // Everything was consumed: nothing is left to arm a quit once the call returns.
    try testing.expect(!ed.pollInterrupt(0));
    _ = std.c.close(in[1]);
}

test "rowMove: Up/Down walk a wrapped line's rows, then hand back to history" {
    // Geometry: prompt "> " (2 cols), a 10-col first row, 12-col continuation rows. The line
    // is 30 bytes, so it occupies rows [0..10), [10..22), [22..30) — three rows.
    const len: usize = 30;
    const first: usize = 10;
    const cols: usize = 12;
    const pl: usize = 2;

    // From the top row there is nothing above: null = "do the history thing instead".
    try testing.expectEqual(@as(?usize, null), rowMove(len, 3, pl, first, cols, true));
    // …and from the last row there is nothing below.
    try testing.expectEqual(@as(?usize, null), rowMove(len, 27, pl, first, cols, false));
    // A line that fits one row keeps Up/Down as previous/next command, wherever the cursor is.
    try testing.expectEqual(@as(?usize, null), rowMove(8, 4, pl, first, cols, true));
    try testing.expectEqual(@as(?usize, null), rowMove(8, 4, pl, first, cols, false));

    // Down from row 0 keeps the SCREEN column: offset 3 sits at column 2+3=5, and row 1 has
    // no prompt in front of it, so the cursor lands 5 bytes into it.
    try testing.expectEqual(@as(?usize, 15), rowMove(len, 3, pl, first, cols, false));
    // Up from there returns to where it started (the move is symmetric).
    try testing.expectEqual(@as(?usize, 3), rowMove(len, 15, pl, first, cols, true));
    // Down from row 1 to row 2, same column, no prompt on either.
    try testing.expectEqual(@as(?usize, 27), rowMove(len, 15, pl, first, cols, false));
    // A column past the end of the target row clamps to the end of the line, never past it.
    try testing.expectEqual(@as(?usize, 30), rowMove(len, 21, pl, first, cols, false));
    // Coming back up onto row 0, the prompt's own columns are not walkable: column 1 is offset 0.
    try testing.expectEqual(@as(?usize, 0), rowMove(len, 10, pl, first, cols, true));
}

test "wrappedRows / rowSlice: the input flows onto as many rows as it needs" {
    // The first row is shorter (the prompt sits on it); an empty line still owns one row.
    try testing.expectEqual(@as(usize, 1), wrappedRows(0, 10, 20));
    try testing.expectEqual(@as(usize, 1), wrappedRows(10, 10, 20));
    try testing.expectEqual(@as(usize, 2), wrappedRows(11, 10, 20));
    try testing.expectEqual(@as(usize, 2), wrappedRows(30, 10, 20));
    try testing.expectEqual(@as(usize, 3), wrappedRows(31, 10, 20));

    const line = "0123456789abcdefghijklmnopqrstuvwxyz";
    try testing.expectEqualStrings("0123456789", rowSlice(line, 0, 10, 8));
    try testing.expectEqualStrings("abcdefgh", rowSlice(line, 1, 10, 8));
    try testing.expectEqualStrings("ijklmnop", rowSlice(line, 2, 10, 8));
    // A row past the end is empty rather than out of bounds.
    try testing.expectEqualStrings("", rowSlice(line, 9, 10, 8));
}

/// Read a non-blocking fd until it runs dry — the editor emits its escapes in many small
/// writes, so one read() would only ever see the first of them.
fn drain(fd: std.posix.fd_t, sink: []u8) []const u8 {
    var n: usize = 0;
    while (n < sink.len) {
        const got = std.c.read(fd, sink[n..].ptr, sink.len - n);
        if (got <= 0) break;
        n += @intCast(got);
    }
    return sink[0..n];
}

test "submitting a wrapped line clears every row it owned, not just the first" {
    // The bug: endPromptLine erased only promptRow(), so the continuation rows of a wrapped
    // prompt stayed on screen for the whole command — nothing else repaints that band.
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();

    // Non-blocking, so drain() can read until the pipe is empty rather than hanging on it.
    const out = try std.Io.Threaded.pipe2(.{ .NONBLOCK = true });
    defer _ = std.c.close(out[0]);
    defer _ = std.c.close(out[1]);
    var scr = screen_mod.Screen{ .gpa = a, .io = threaded.io(), .fd = out[1], .rows = 20, .cols = 40 };
    defer scr.freeAllForTest();
    var ed = Editor{ .fd_in = out[0], .fd_out = out[1], .orig = std.mem.zeroes(std.posix.termios), .screen = &scr };

    // A line long enough to need three rows at 40 columns with a 2-column prompt.
    var long: [100]u8 = undefined;
    @memset(&long, 'x');
    ed.refresh("> ", &long, long.len);
    try testing.expectEqual(@as(u16, 3), scr.promptRows());

    // Drain what the refresh wrote, then submit: every row of the block must be erased…
    var sink: [65536]u8 = undefined;
    _ = drain(out[0], &sink);
    ed.endPromptLine();
    const written = drain(out[0], &sink);
    // Rows 18, 19 and 20 are the block on a 20-row screen: each is addressed and erased.
    try testing.expect(std.mem.indexOf(u8, written, "\x1b[18;1H\x1b[2K") != null);
    try testing.expect(std.mem.indexOf(u8, written, "\x1b[19;1H\x1b[2K") != null);
    try testing.expect(std.mem.indexOf(u8, written, "\x1b[20;1H\x1b[2K") != null);
    // …and the block shrinks back to one row, giving the output area its rows back.
    try testing.expectEqual(@as(u16, 1), scr.promptRows());
}

test "refresh keeps a selection wash on the input rows instead of erasing it" {
    // The bug: every mouse event ends in refresh(), which repaints the input rows — wiping the
    // highlight the screen had just drawn there, so a drag over the typed line showed nothing.
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();

    const out = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(out[0]);
    defer _ = std.c.close(out[1]);
    var scr = screen_mod.Screen{ .gpa = a, .io = threaded.io(), .fd = out[1], .rows = 10, .cols = 40 };
    defer scr.freeAllForTest();

    var ed = Editor{ .fd_in = out[0], .fd_out = out[1], .orig = std.mem.zeroes(std.posix.termios), .screen = &scr };
    const line = "hello world";

    // No selection: the row is painted plainly.
    ed.refresh("> ", line, line.len);
    try testing.expect(!scr.hasHighlight());

    // With one covering the input row, a refresh must still leave the wash on screen.
    scr.selectForTest(scr.promptRow(), 3, scr.promptRow(), 8);
    var drained: [8192]u8 = undefined;
    _ = std.posix.read(out[0], &drained) catch 0;   // ignore what came before
    ed.refresh("> ", line, line.len);
    const n = std.posix.read(out[0], &drained) catch 0;
    const painted = drained[0..n];
    try testing.expect(std.mem.indexOf(u8, painted, "\x1b[48;2;") != null); // the wash survived
}

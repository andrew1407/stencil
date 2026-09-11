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
const wrap = @import("line_edit/wrap.zig");
const markers = @import("line_edit/markers.zig");
const words = @import("line_edit/words.zig");
pub const History = @import("line_edit/history.zig").History;
pub const max_history = @import("line_edit/history.zig").max_history;
pub const PendingImages = markers.PendingImages;
pub const max_pending_images = markers.max_pending_images;
pub const max_marker_label = markers.max_marker_label;
pub const marker_open = markers.marker_open;
pub const markerEnd = markers.markerEnd;
pub const markerBefore = markers.markerBefore;
pub const stripMarkers = markers.stripMarkers;
pub const sanitizeInline = markers.sanitizeInline;
pub const max_prompt_rows = wrap.max_prompt_rows;
pub const wrappedRows = wrap.wrappedRows;
pub const rowSlice = wrap.rowSlice;
pub const rowStart = wrap.rowStart;
pub const rowMove = wrap.rowMove;
pub const isWordChar = words.isWordChar;
pub const wordLeft = words.wordLeft;
pub const wordRight = words.wordRight;
pub const commonLen = words.commonLen;
pub const copyInto = words.copyInto;

pub const max_line = 4096; // editing buffer size; commands (URLs, crop specs) fit easily

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

// the editor (raw terminal mode, restored on deinit)

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

    pub const ByteResult = union(enum) { byte: u8, idle, closed };
    // Two logo clicks within this window = double-click, and a SINGLE click is deferred this
    // long before it cycles the accent. That wait — plus the press frame held before it
    // (screen.zig press_ms) — is the whole lag between the click and the colour moving, and
    // both were halved to cut it in two. 250ms is also the interval the browser app's own
    // deferred click uses (ui/popover.js DOUBLE_CLICK_MS), so a double-click stays comfortable.
    pub const double_click_ms: i64 = 250;

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
    pub const writeAll = @import("line_edit/render.zig").writeAll;
    pub const wrapGeom = @import("line_edit/render.zig").wrapGeom;
    pub const refresh = @import("line_edit/render.zig").refresh;
    pub const refreshFlat = @import("line_edit/render.zig").refreshFlat;
    pub const gotoRow = @import("line_edit/render.zig").gotoRow;
    pub const gotoLineStart = @import("line_edit/render.zig").gotoLineStart;
    pub const endPromptLine = @import("line_edit/render.zig").endPromptLine;
    pub const clearPromptBlock = @import("line_edit/render.zig").clearPromptBlock;
    pub const nowMs = @import("line_edit/keys.zig").nowMs;
    pub const readByte = @import("line_edit/keys.zig").readByte;
    pub const pollByte = @import("line_edit/keys.zig").pollByte;

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
    pub const waitReadable = @import("line_edit/keys.zig").waitReadable;
    pub const drainMouseRelease = @import("line_edit/keys.zig").drainMouseRelease;
    pub const clearForOutput = @import("line_edit/render.zig").clearForOutput;
    pub const attachClipboard = @import("line_edit/paste.zig").attachClipboard;
    pub const copySelection = @import("line_edit/paste.zig").copySelection;
    pub const clearLineTrampoline = @import("line_edit/render.zig").clearLineTrampoline;
    pub const armLineClear = @import("line_edit/render.zig").armLineClear;
    pub const dropLastMarker = @import("line_edit/paste.zig").dropLastMarker;
    pub const abortPending = @import("line_edit/paste.zig").abortPending;
    pub const syncPending = @import("line_edit/paste.zig").syncPending;
    pub const insertMarker = @import("line_edit/editing.zig").insertMarker;
    pub const insertText = @import("line_edit/editing.zig").insertText;

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
    pub const drainCsi = @import("line_edit/keys.zig").drainCsi;

    fn finishConfirm(self: *Editor, question: []const u8, yes: bool) void {
        if (self.screen != null) {
            logo.print("{s} {s}\n", .{ question, if (yes) "yes" else "no" }); // record in scrollback
            self.gotoLineStart();
            self.writeAll("\x1b[2K");
        } else {
            self.writeAll(if (yes) "yes\r\n" else "no\r\n");
        }
    }

    pub const CsiResult = struct { np: usize, final: u8 };
    pub const collectCsi = @import("line_edit/keys.zig").collectCsi;
    pub const collectCsi2 = @import("line_edit/keys.zig").collectCsi2;
    pub const csi = @import("line_edit/keys.zig").csi;
    pub const backspace = @import("line_edit/editing.zig").backspace;
    pub const deleteWordBack = @import("line_edit/editing.zig").deleteWordBack;
    pub const deleteWordFwd = @import("line_edit/editing.zig").deleteWordFwd;
    pub const readPaste = @import("line_edit/paste.zig").readPaste;
    pub const attachPastedPath = @import("line_edit/paste.zig").attachPastedPath;
    pub const handleMouse = @import("line_edit/paste.zig").handleMouse;
    pub const rowStep = @import("line_edit/editing.zig").rowStep;
    pub const recall = @import("line_edit/editing.zig").recall;
    pub const complete = @import("line_edit/editing.zig").complete;
    pub const listMatches = @import("line_edit/render.zig").listMatches;
};

// Write "[/]name[ ]" into buf and return the new length (clamped to the buffer).
pub fn setCommand(buf: []u8, slash: bool, name: []const u8, space: bool) usize {
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

test {
    _ = wrap;
    _ = markers;
    _ = words;
    _ = @import("line_edit/history.zig");
    _ = @import("line_edit/render.zig");
    _ = @import("line_edit/keys.zig");
    _ = @import("line_edit/editing.zig");
    _ = @import("line_edit/paste.zig");
}

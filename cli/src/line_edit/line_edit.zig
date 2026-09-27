//! A minimal raw-mode line editor for the interactive console: cursor motion, backspace/delete,
//! Home/End, Tab to complete the command word, Up/Down over an in-session history. Ctrl-V attaches a
//! clipboard image to the line as an `[Image #N <name>]` marker — the editor owns the markers, the
//! host (console.zig) owns the pictures, through the PendingImages hooks. Echoing is by hand (ECHO is
//! off) so the prompt renders in the brand accent. TTY only; piped input keeps the plain reader.
const std = @import("std");
const screen_mod = @import("../console/screen.zig");
const terminal = @import("../console/screen/terminal.zig");
const wrap = @import("wrap.zig");
const markers = @import("markers.zig");
const words = @import("words.zig");
pub const History = @import("history.zig").History;
pub const max_history = @import("history.zig").max_history;
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

// What a readLine() call resolved to: a submitted line carries its length in `buf`, the other
// variants are key chords the caller acts on, so line_edit stays free of session/image knowledge.
pub const Input = union(enum) {
    line: usize, // a command line of this many bytes now sits in `buf`
    eof, // Ctrl-D or a closed tty — leave the console immediately
    interrupt, // Ctrl-C — caller confirms exit (twice)
    copy, // Ctrl-Alt-C — caller copies the image to the clipboard
    paste, // Ctrl-V (or Ctrl-Alt-V) — caller loads an image from the clipboard
    unpaste, // Ctrl-Z (or Ctrl-Alt-Z) — caller takes back the last image added this turn
};

/// What a Ctrl-V found on the clipboard: a picture (label length), plain text (byte length), or
/// nothing at all.
pub const PasteResult = union(enum) { image: usize, text: usize, none };

// the editor (raw terminal mode, restored on deinit)

pub const Editor = struct {
    fd_in: std.posix.fd_t,
    fd_out: std.posix.fd_t,
    orig: std.posix.termios,
    // The byte this tty sends for a plain Backspace (termios VERASE — 0x7f nearly everywhere, 0x08 on a
    // few). Whichever it is NOT means Ctrl-Backspace, which every desktop expects to delete a WORD.
    erase: u8 = 127,
    paste: bool = false, // bracketed paste was switched on (stderr is a terminal)
    unread: [64]u8 = undefined, // type-ahead a watch read while a call ran, read again first
    unread_len: u8 = 0,
    // Optional idle hook, invoked when the input read times out (no key for ~idle_ms) so the REPL can
    // poll the live events feed. readLine clears the prompt line before the call and redraws it after.
    idle_cb: ?*const fn (*anyopaque) bool = null, // returns true if it printed → repaint the prompt
    idle_ctx: ?*anyopaque = null,

    // Full-screen ("screen mode") wiring, all null in the plain line-oriented mode. With `screen` set the
    // prompt is drawn at its fixed bottom row and mouse wheel / logo clicks drive the screen directly.
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
    // Two logo clicks within this window = double-click, and a SINGLE click is deferred this long before
    // it cycles the accent. 250ms is the browser app's own DOUBLE_CLICK_MS (ui/tip/popover.js).
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
        terminal.holdRaw(tty_fd, orig);
        const erase = orig.cc[@intFromEnum(std.posix.V.ERASE)];
        // Bracketed paste (ESC[200~ … ESC[201~): a multi-line paste lands in the buffer as one line.
        // Asked of the terminal on stderr, so only when stderr IS one.
        const out = std.posix.STDERR_FILENO;
        const paste = std.c.isatty(out) == 1;
        if (paste) {
            _ = std.c.write(out, "\x1b[?2004h", 8);
            terminal.holdPaste(out);
        }
        return .{ .fd_in = tty_fd, .fd_out = out, .orig = orig, .erase = if (erase != 0) erase else 127, .paste = paste };
    }

    pub fn deinit(self: *Editor) void {
        // Hand the terminal back exactly as we found it: no lingering colour/attribute from a
        // half-written accent span, and its own paste mode.
        if (self.paste) {
            _ = std.c.write(self.fd_out, "\x1b[0m", 4);
            _ = std.c.write(self.fd_out, "\x1b[?2004l", 8); // disable bracketed paste
            terminal.releasePaste();
        }
        std.posix.tcsetattr(self.fd_in, .FLUSH, self.orig) catch {};
        terminal.releaseRaw();
    }
    pub const writeAll = @import("render.zig").writeAll;
    pub const wrapGeom = @import("render.zig").wrapGeom;
    pub const refresh = @import("render.zig").refresh;
    pub const refreshFlat = @import("render.zig").refreshFlat;
    pub const gotoRow = @import("render.zig").gotoRow;
    pub const gotoLineStart = @import("render.zig").gotoLineStart;
    pub const endPromptLine = @import("render.zig").endPromptLine;
    pub const clearPromptBlock = @import("render.zig").clearPromptBlock;
    pub const nowMs = @import("keys.zig").nowMs;
    pub const readByte = @import("keys.zig").readByte;
    pub const pollByte = @import("keys.zig").pollByte;
    pub const pollNext = @import("keys.zig").pollNext;
    pub const pollKeep = @import("keys.zig").pollKeep;

    /// Watch the tty for up to `timeout_ms` while a long command runs: true when Ctrl-C was pressed.
    /// Everything else readable is DROPPED, so a press mid-call means "cancel this", never "quit later".
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
    pub const waitReadable = @import("keys.zig").waitReadable;
    pub const drainMouseRelease = @import("keys.zig").drainMouseRelease;
    pub const clearForOutput = @import("render.zig").clearForOutput;
    pub const attachClipboard = @import("paste.zig").attachClipboard;
    pub const copySelection = @import("paste.zig").copySelection;
    pub const clearLineTrampoline = @import("render.zig").clearLineTrampoline;
    pub const armLineClear = @import("render.zig").armLineClear;
    pub const dropLastMarker = @import("paste.zig").dropLastMarker;
    pub const abortPending = @import("paste.zig").abortPending;
    pub const syncPending = @import("paste.zig").syncPending;
    pub const insertMarker = @import("editing.zig").insertMarker;
    pub const insertText = @import("editing.zig").insertText;

    pub const readLine = @import("read.zig").readLine;
    pub const confirm = @import("confirm.zig").confirm;
    pub const readSecret = @import("secret.zig").readSecret;
    pub const maskCommitted = @import("secret.zig").maskCommitted;
    pub const finishConfirm = @import("confirm.zig").finishConfirm;
    pub const drainCsi = @import("keys.zig").drainCsi;
    pub const drainString = @import("keys.zig").drainString;

    pub const CsiResult = struct { np: usize, final: u8 };
    pub const collectCsi = @import("keys.zig").collectCsi;
    pub const collectCsi2 = @import("keys.zig").collectCsi2;
    pub const csi = @import("keys.zig").csi;
    pub const backspace = @import("editing.zig").backspace;
    pub const deleteWordBack = @import("editing.zig").deleteWordBack;
    pub const deleteWordFwd = @import("editing.zig").deleteWordFwd;
    pub const readPaste = @import("paste.zig").readPaste;
    pub const attachPastedPath = @import("paste.zig").attachPastedPath;
    pub const handleMouse = @import("paste.zig").handleMouse;
    pub const rowStep = @import("editing.zig").rowStep;
    pub const recall = @import("editing.zig").recall;
    pub const complete = @import("editing.zig").complete;
    pub const listMatches = @import("render.zig").listMatches;
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

test {
    _ = wrap;
    _ = markers;
    _ = words;
    _ = @import("history.zig");
    _ = @import("render.zig");
    _ = @import("keys.zig");
    _ = @import("editing.zig");
    _ = @import("paste.zig");
}

test {
    _ = @import("read.zig");
    _ = @import("confirm.zig");
    _ = @import("secret.zig");
}

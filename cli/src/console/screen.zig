//! Full-screen ("TUI") console renderer for the interactive REPL: pinned logo header,
//! scrollback body, status rule, prompt on the bottom row. SGR mouse tracking drives logo
//! clicks (accent cycle, mirroring the browser logo), wheel scroll, and drag-selection;
//! every `logo.print` is routed here via the output sink, and everything degrades to the
//! plain line editor when the terminal is too small or size detection fails.
const std = @import("std");
const logo = @import("../logo.zig");
const ansi = @import("ansi.zig");
const logoFx = @import("logoFx.zig");

// One active screen at a time; handlers reach it (for a theme repaint) via `current()`.
var g_screen: ?*Screen = null;
pub fn current() ?*Screen {
    return g_screen;
}

const max_lines = 5000; // scrollback cap; oldest lines drop past this
const wheel_step = 3; // rows per wheel notch
pub const header_pad = 1; // blank rows between the logo header and the output
// `/reveal-speed <speed>`, 0.01 … 1: 1 = instant, smaller = slower (0 would never finish).
// The constants above are the pace at the default 0.5; each scales by (1-speed)/speed,
// which is 1× there. The quiet/burst windows scale too.
const reveal_speed_min = 0.01;
const reveal_speed_max = 1.0;
const reveal_speed_default = 0.5;

pub const Screen = struct {
    gpa: std.mem.Allocator,
    io: std.Io,
    fd: std.posix.fd_t = std.posix.STDERR_FILENO,
    in_fd: ?std.posix.fd_t = null, // input tty fd — polled to abort the flourish when a click is queued
    // The terminal's OWN highlight colour (its `OSC 17;?` answer), restored on exit —
    // the documented reset `OSC 117` is honoured by fewer terminals than `OSC 17`.
    saved_hl: [64]u8 = undefined,
    saved_hl_len: usize = 0,

    rows: u16 = 24,
    cols: u16 = 80,
    prompt_rows: u16 = 1, // rows the input block owns (grows as a typed line wraps)
    // What the line editor last painted on those rows (owned). The typed text lives in the
    // editor, not in the scrollback, so selecting it needs a copy here.
    prompt_lines: std.ArrayList([]u8) = .empty,
    header: std.ArrayList([]u8) = .empty, // pinned logo lines (owned)
    // Where a banner capture lands when it is NOT the pinned header — the shrunken logo the
    // click animation flashes. Set only for the duration of that capture.
    alt_capture: ?*std.ArrayList([]u8) = null,
    // The accent the screen is currently painted in. `logo`'s accent has already moved on by
    // the time onThemeChanged runs, so the outgoing colour has to be remembered here for the
    // sweep to paint the not-yet-covered side of the screen in it.
    painted_accent: [3]u8 = .{ 124, 58, 237 },
    // How far right the accent reaches on screen, measured when a recolour starts. The seam
    // travels this far, the rule scales its own faster progress against it, and the icon's
    // clock hand takes the whole turn over the same distance.
    wipe_reach: u16 = 0,
    lines: std.ArrayList([]u8) = .empty, // scrollback (owned)
    pending: std.ArrayList(u8) = .empty, // partial line being accumulated
    hdr_pending: std.ArrayList(u8) = .empty, // partial header line (during capture)
    capturing_header: bool = false,
    scroll_off: usize = 0, // lines scrolled up from the live bottom (0 = live)
    mouse_on: bool = false, // SGR mouse reporting state (toggled by /mouse)
    reveal_speed: f64 = reveal_speed_default, // how fast new output sweeps in (1 = instantly)
    skip_reveal_once: bool = false, // next append lands at once (the echo of a typed command)
    reveal_last_ns: i96 = 0, // when the last sweep finished (0 = none yet) — burst detection
    reveal_burst_ns: i96 = 0, // when the current burst of output began
    wordmark_row: u16 = 0, // 1-based screen row of "S T E N C I L" (0 = not found)
    wordmark_col: u16 = 0, // 1-based starting column of the wordmark
    // In-app text selection (drag to highlight) — works while mouse tracking is on, which would
    // otherwise deny native selection. Extracted on release, copied only on Ctrl-S. 1-based cells.
    sel_active: bool = false, // a drag is in progress
    has_sel: bool = false, // a highlight is currently drawn
    sel_ar: u16 = 0, // anchor (drag start) row/col
    sel_ac: u16 = 0,
    sel_hr: u16 = 0, // head (current) row/col
    sel_hc: u16 = 0,
    sel_buf: std.ArrayList(u8) = .empty, // extracted selection text (kept until copied or cleared)

    pub const Error = error{ TerminalTooSmall, SizeUnavailable };

    // ── lifecycle ──────────────────────────────────────────────────────────────

    /// Enter full-screen mode; on any failure tears down cleanly and errors so the caller
    /// falls back to the plain editor. `self` must have a stable address for the session
    /// (its pointer is handed to the sink and to `g_screen`).
    pub fn start(self: *Screen) Error!void {
        try self.querySize();
        self.queryHighlight();
        // Capture the banner into header lines by routing logo.print at ourselves first.
        logo.setSink(sinkTrampoline, self);
        errdefer logo.clearSink();
        self.captureHeader();
        if (self.rows < self.headerRows() + 4 or self.cols < 8) {
            self.freeAll();
            return Error.TerminalTooSmall;
        }
        // Alt screen + no autowrap (a full-width write must never wrap and scroll the pinned
        // header off — this is what would otherwise "eat" the logo).
        ttyWrite(self.fd, "\x1b[?1049h\x1b[?7l");
        // Mouse tracking is ON by default; terminals keep a native-selection escape hatch
        // (Shift/Option+drag), and `/mouse off` or STENCIL_CONSOLE_MOUSE hands the mouse back.
        self.setMouse(mousePreference() orelse true);
        self.reveal_speed = revealSpeedPreference() orelse reveal_speed_default; // STENCIL_CONSOLE_REVEAL_SPEED sets your own
        logo.setAccentSentinel(true); // stored accent spans re-tint to the live accent on repaint
        self.painted_accent = logo.accentRgb();
        g_screen = self;
        self.fullPaint();
    }

    pub fn deinit(self: *Screen) void {
        logo.clearSink();
        logo.setAccentSentinel(false);
        self.setMouse(false);
        self.setSelectionTint(false); // the terminal keeps its own selection colour after us
        // Restore: re-enable autowrap, leave the alternate screen.
        ttyWrite(self.fd, "\x1b[?7h\x1b[?1049l");
        g_screen = null;
        self.freeAll();
    }

    pub fn mouseOn(self: *Screen) bool {
        return self.mouse_on;
    }

    /// Ask the terminal for its current highlight colour (`OSC 17;?` → `rgb:rrrr/gggg/bbbb`).
    /// Best-effort with a short deadline; runs once at startup, before anything can be typed.
    fn queryHighlight(self: *Screen) void {
        self.saved_hl_len = 0;
        if (!logo.colorEnabled()) return;
        const in = self.in_fd orelse return;
        ttyWrite(self.fd, "\x1b]17;?\x1b\\");
        // ESC ] 1 7 ; <payload> (ESC \ | BEL). Anything unexpected ends the read at once, so a
        // terminal that stays silent — or a keystroke that beat the reply — costs one poll.
        const prefix = "\x1b]17;";
        var i: usize = 0;
        while (i < prefix.len) : (i += 1) {
            const b = readByteTimeout(in, 120) orelse return;
            if (b != prefix[i]) return;
        }
        var n: usize = 0;
        while (n < self.saved_hl.len) {
            const b = readByteTimeout(in, 120) orelse return;
            if (b == 7) break; // BEL terminator
            if (b == 0x1b) { // ST: ESC \
                _ = readByteTimeout(in, 120);
                break;
            }
            self.saved_hl[n] = b;
            n += 1;
        }
        self.saved_hl_len = n;
    }

    /// The user's standing answer to "whose selection?" — STENCIL_CONSOLE_MOUSE=on gives the
    /// in-app accent one (Ctrl-S copies), =off keeps the terminal's. Null = decide per terminal.
    fn mousePreference() ?bool {
        const raw = std.c.getenv("STENCIL_CONSOLE_MOUSE") orelse return null;
        const v = std.mem.span(raw);
        if (std.ascii.eqlIgnoreCase(v, "on") or std.mem.eql(u8, v, "1")) return true;
        if (std.ascii.eqlIgnoreCase(v, "off") or std.mem.eql(u8, v, "0")) return false;
        return null;
    }

    /// The user's standing answer to "how fast should text appear?" —
    /// STENCIL_CONSOLE_REVEAL_SPEED=<0.01…1>, or the words `off` (= 1, instantly) and `on`
    /// (= the default). Null = unset or unparseable, so the default stands.
    fn revealSpeedPreference() ?f64 {
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

    /// Ask the TERMINAL to paint its own selection in the live accent (OSC 17 sets the
    /// highlight background; OSC 117 puts it back). Terminals without OSC 17 ignore it,
    /// so it is safe to send anywhere.
    fn setSelectionTint(self: *Screen, on: bool) void {
        if (!logo.colorEnabled()) return;
        if (!on) {
            // Put back the exact colour the terminal had, when it told us; else ask for its
            // default with the reset opcode.
            if (self.saved_hl_len != 0) {
                var b: [80]u8 = undefined;
                const seq = std.fmt.bufPrint(&b, "\x1b]17;{s}\x1b\\", .{self.saved_hl[0..self.saved_hl_len]}) catch return;
                return ttyWrite(self.fd, seq);
            }
            return ttyWrite(self.fd, "\x1b]117\x1b\\");
        }
        const rgb = logo.accentRgb();
        var buf: [40]u8 = undefined;
        const seq = std.fmt.bufPrint(&buf, "\x1b]17;#{x:0>2}{x:0>2}{x:0>2}\x1b\\", .{ rgb[0], rgb[1], rgb[2] }) catch return;
        ttyWrite(self.fd, seq);
    }

    pub fn setMouse(self: *Screen, on: bool) void {
        if (on and self.mouse_on) return; // already on (a disable always re-emits, for safe teardown)
        self.mouse_on = on;
        // 1002 = button + drag-motion reporting (needed for the drag-to-highlight visual), 1006 = SGR coords.
        ttyWrite(self.fd, if (on) "\x1b[?1002h\x1b[?1006h" else "\x1b[?1002l\x1b[?1006l");
        // Whichever selection the user is left with wears the accent: ours is painted in-app,
        // the terminal's is tinted through OSC 17.
        self.setSelectionTint(!on);
        if (g_screen == self) self.drawStatusBar(); // reflect the state in the rule hint
    }

    fn freeAll(self: *Screen) void {
        for (self.prompt_lines.items) |l| self.gpa.free(l);
        self.prompt_lines.deinit(self.gpa);
        for (self.header.items) |l| self.gpa.free(l);
        self.header.deinit(self.gpa);
        for (self.lines.items) |l| self.gpa.free(l);
        self.lines.deinit(self.gpa);
        self.pending.deinit(self.gpa);
        self.hdr_pending.deinit(self.gpa);
        self.sel_buf.deinit(self.gpa);
    }

    // One byte from `fd` within `ms`, or null (timeout / closed). Used only by the startup
    // colour query — the line editor does its own polling.
    fn readByteTimeout(fd: std.posix.fd_t, ms: i32) ?u8 {
        var pfd = [_]std.posix.pollfd{.{ .fd = fd, .events = std.posix.POLL.IN, .revents = 0 }};
        const ready = std.posix.poll(&pfd, ms) catch return null;
        if (ready == 0) return null;
        var b: [1]u8 = undefined;
        const n = std.posix.read(fd, &b) catch return null;
        return if (n == 0) null else b[0];
    }

    // ── geometry ────────────────────────────────────────────────────────────────

    pub fn headerRows(self: *Screen) u16 {
        return @intCast(self.header.items.len);
    }
    /// Max rows available for content (between the header and the rule + prompt at the bottom).
    pub fn bodyRows(self: *Screen) u16 {
        const used = self.headerRows() + 1 + self.prompt_rows; // 1 rule row + the input block
        return if (self.rows > used) self.rows - used else 0;
    }

    /// How many rows the input block occupies. The line editor sets this as a typed line wraps,
    /// so the output above simply gets shorter instead of being written over. Repaints when the
    /// height actually changes; capped so a very long paste can never swallow the window.
    pub fn setPromptRows(self: *Screen, want: u16) void {
        const max: u16 = @max(@as(u16, 1), @min(@as(u16, 8), self.rows / 2));
        const n = @max(@as(u16, 1), @min(want, max));
        if (n == self.prompt_rows) return;
        self.prompt_rows = n;
        self.clampScroll();
        self.fullPaint();
    }

    /// Take the input block's freshly painted rows, so a drag can select the line being typed
    /// (the row the mouse most wants and the only one the scrollback never sees).
    pub fn setPromptText(self: *Screen, rows: []const []const u8) void {
        for (self.prompt_lines.items) |l| self.gpa.free(l);
        self.prompt_lines.clearRetainingCapacity();
        for (rows) |r| {
            const dup = self.gpa.dupe(u8, r) catch return;
            self.prompt_lines.append(self.gpa, dup) catch {
                self.gpa.free(dup);
                return;
            };
        }
    }

    /// The text on screen row `row`: a scrollback line, or one of the input rows.
    fn lineAtRow(self: *Screen, row: u16) ?[]const u8 {
        if (row >= self.promptRow()) {
            const i: usize = row - self.promptRow();
            return if (i < self.prompt_lines.items.len) self.prompt_lines.items[i] else null;
        }
        if (row < self.bodyTop() or row > self.bodyBottom()) return null;
        const w = self.window();
        const idx = w.first + (row - self.bodyTop());
        return if (idx < w.end) self.lines.items[idx] else null;
    }

    /// Rows a drag may select: the output body and the input block (never the logo header).
    fn selectableRow(self: *Screen, row: u16) bool {
        if (row >= self.bodyTop() and row <= self.bodyBottom()) return true;
        return row >= self.promptRow() and row <= self.rows;
    }

    /// The cap `setPromptRows` clamps to — the editor uses it to decide when to scroll the
    /// input inside its block instead of growing it further.
    pub fn maxPromptRows(self: *Screen) u16 {
        return @max(@as(u16, 1), @min(@as(u16, 8), self.rows / 2));
    }
    // The half-open slice [first,end) of `lines` that the body viewport currently shows, honouring
    // the scroll offset. The single source of truth for every paint/extract loop.
    const Window = struct { first: usize, end: usize };
    pub fn window(self: *Screen) Window {
        const bh = self.bodyRows();
        const n = self.lines.items.len;
        const max_off = if (n > bh) n - bh else 0;
        const off = @min(self.scroll_off, max_off);
        const end = n - off;
        const shown = @min(@as(usize, bh), end);
        return .{ .first = end - shown, .end = end };
    }
    // How many content rows are actually shown right now (fewer than bodyRows when there's
    // little output) — this is what lets the rule + prompt float up just below the output.
    fn contentShown(self: *Screen) u16 {
        const w = self.window();
        return @intCast(w.end - w.first);
    }
    // The prompt owns the bottom row and the rule sits just above it, both PINNED to the
    // bottom; output fills the gap top-aligned. start() guarantees rows >= headerRows()+4.
    fn statusRow(self: *Screen) u16 {
        return self.rows - self.prompt_rows;
    }
    /// The FIRST row of the input block (it grows downward to the bottom of the screen).
    pub fn promptRow(self: *Screen) u16 {
        return self.rows - self.prompt_rows + 1;
    }
    /// How many rows that block currently owns — the editor clears all of them when the line
    /// it was holding goes away.
    pub fn promptRows(self: *Screen) u16 {
        return self.prompt_rows;
    }
    pub fn inHeader(self: *Screen, row: u16) bool {
        return row >= 1 and row <= self.headerRows();
    }

    // ── in-app text selection (visual only) ────────────────────────────────────────

    const SelRange = struct { sr: u16, sc: u16, er: u16, ec: u16 };

    fn bodyTop(self: *Screen) u16 {
        return self.headerRows() + 1;
    }
    fn bodyBottom(self: *Screen) u16 {
        return self.headerRows() + self.contentShown();
    }

    pub fn selActive(self: *Screen) bool {
        return self.sel_active;
    }

    /// Begin a drag-selection at (col,row). Ignored (and any prior highlight cleared) unless the
    /// press lands on an output row — a press on the logo header stays a logo click.
    pub fn selStart(self: *Screen, col: u16, row: u16) void {
        const had = self.has_sel;
        self.has_sel = false; // no highlight until an actual drag extends the selection
        self.sel_active = false;
        if (!self.selectableRow(row)) {
            if (had) self.repaintSelection(); // clear a stale highlight
            return;
        }
        self.sel_active = true;
        self.sel_ar = row;
        self.sel_ac = col;
        self.sel_hr = row;
        self.sel_hc = col;
        if (had) self.repaintSelection(); // clear a stale highlight
    }

    /// Extend the in-progress selection to (col,row) and repaint the live highlight.
    pub fn selDrag(self: *Screen, col: u16, row: u16) void {
        if (!self.sel_active) return;
        // The drag may run from the output down into the input block, so it clamps to the
        // bottom of the screen rather than to the last output row.
        self.sel_hr = std.math.clamp(row, self.bodyTop(), self.rows);
        self.sel_hc = @max(@as(u16, 1), @min(col, self.cols));
        self.has_sel = true;
        self.repaintSelection();
    }

    /// Finish the drag: extract the highlighted text into `sel_buf` and KEEP the highlight on
    /// screen. Nothing is copied until Ctrl-S. No-op for a plain click (no drag).
    pub fn selEnd(self: *Screen) void {
        if (!self.sel_active) return;
        self.sel_active = false;
        if (!self.has_sel) return; // a click without a drag selects nothing
        self.extractSelection();
        self.repaintSelection(); // settle the final highlight (drag is over)
    }

    /// Whether a finished, still-highlighted selection is present (and thus copyable via Ctrl-S).
    pub fn hasSelection(self: *Screen) bool {
        return self.has_sel and !self.sel_active;
    }

    /// Take the current selection's text for the clipboard and clear the highlight. "" if none.
    pub fn takeSelection(self: *Screen) []const u8 {
        if (!self.has_sel) return "";
        self.has_sel = false;
        self.sel_active = false;
        self.repaintSelection();
        return self.sel_buf.items;
    }

    // Normalise anchor/head into reading order (top-left → bottom-right).
    fn selNorm(self: *Screen) SelRange {
        var sr = self.sel_ar;
        var sc = self.sel_ac;
        var er = self.sel_hr;
        var ec = self.sel_hc;
        if (er < sr or (er == sr and ec < sc)) {
            sr = self.sel_hr;
            sc = self.sel_hc;
            er = self.sel_ar;
            ec = self.sel_ac;
        }
        return .{ .sr = sr, .sc = sc, .er = er, .ec = ec };
    }

    // The highlighted visible-column half-open range [c0,c1) (0-based) for screen `row`, or null
    // when the row is outside the selection. Start row runs from its column to the line's end;
    // the end row from the line start to its column; middle rows are full width.
    fn selRowCols(self: *Screen, row: u16) ?struct { c0: u16, c1: u16 } {
        if (!self.has_sel) return null;
        const s = self.selNorm();
        if (row < s.sr or row > s.er) return null;
        const c0: u16 = if (row == s.sr) s.sc - 1 else 0;
        const c1: u16 = if (row == s.er) s.ec else self.cols;
        return .{ .c0 = c0, .c1 = c1 };
    }

    // Gather the selected text into sel_buf: for each selected screen row, the buffer line's
    // visible characters within the row's column range, joined with newlines (trailing blanks
    // trimmed). Runs against the currently-visible window (a drag doesn't scroll).
    fn extractSelection(self: *Screen) void {
        self.sel_buf.clearRetainingCapacity();
        if (self.bodyRows() == 0) return;
        const s = self.selNorm();
        var row: u16 = s.sr;
        while (row <= s.er) : (row += 1) {
            const line = self.lineAtRow(row) orelse continue; // the gap above the rule
            const c0: u16 = if (row == s.sr) s.sc - 1 else 0;
            const c1: u16 = if (row == s.er) s.ec else self.cols;
            var seg: [8192]u8 = undefined;
            const text = ansi.visibleSlice(line, c0, c1, &seg);
            var tlen = text.len;
            while (tlen > 0 and text[tlen - 1] == ' ') tlen -= 1; // trim trailing spaces
            self.sel_buf.appendSlice(self.gpa, text[0..tlen]) catch return;
            if (row != s.er) self.sel_buf.append(self.gpa, '\n') catch return;
        }
    }

    fn querySize(self: *Screen) Error!void {
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

    // ── header capture (reuses logo.banner via the sink) ─────────────────────────

    /// Drop the blank leading/trailing lines a banner capture emits, so the block hugs its rows.
    pub fn trimBlankEnds(self: *Screen, list: *std.ArrayList([]u8)) void {
        while (list.items.len != 0 and list.items[0].len == 0) {
            self.gpa.free(list.orderedRemove(0));
        }
        while (list.items.len != 0 and list.items[list.items.len - 1].len == 0) {
            self.gpa.free(list.pop().?);
        }
    }

    fn captureHeader(self: *Screen) void {
        for (self.header.items) |l| self.gpa.free(l);
        self.header.clearRetainingCapacity();
        self.hdr_pending.clearRetainingCapacity();
        self.capturing_header = true;
        logo.banner();
        self.capturing_header = false;
        self.trimBlankEnds(&self.header);
        // Locate the "S T E N C I L" wordmark (row + starting visible column) for the
        // theme-change animation, before we append the padding rows.
        self.wordmark_row = 0;
        for (self.header.items, 0..) |line, idx| {
            if (std.mem.indexOf(u8, line, logoFx.wordmark)) |off| {
                self.wordmark_row = @intCast(idx + 1); // 1-based screen row
                self.wordmark_col = @intCast(ansi.visColumns(line[0..off]) + 1);
                break;
            }
        }
        // Padding rows between the logo and the output, so text never butts against the logo.
        var p: usize = 0;
        while (p < header_pad) : (p += 1) {
            const blank = self.gpa.dupe(u8, "") catch break;
            self.header.append(self.gpa, blank) catch {
                self.gpa.free(blank);
                break;
            };
        }
    }

    /// Flash the logo one cell smaller and back — the pressed state of a button
    /// for a click on the pinned logo (logoFx owns the animation).
    pub const pressLogo = logoFx.pressLogo;

    /// Recapture the header in the new accent, sweep the new colour across the screen, then play
    /// the logo animation — called after a `/theme` change or a logo click.
    pub fn onThemeChanged(self: *Screen) void {
        if (!self.mouse_on) self.setSelectionTint(true); // the terminal's own highlight follows too
        // Take the outgoing header (it still carries the old accent) before recapturing, so the
        // sweep has both renderings of every logo row to splice together.
        var old_header = self.header;
        self.header = .empty;
        defer {
            for (old_header.items) |l| self.gpa.free(l);
            old_header.deinit(self.gpa);
        }
        self.captureHeader();
        logoFx.wipeRecolor(self, old_header.items);
        self.painted_accent = logo.accentRgb();
    }

    // ── output sink ──────────────────────────────────────────────────────────────

    fn sinkTrampoline(ctx: *anyopaque, bytes: []const u8) void {
        const self: *Screen = @ptrCast(@alignCast(ctx));
        if (self.alt_capture) |dst|
            pushChunk(self.gpa, dst, &self.hdr_pending, bytes)
        else if (self.capturing_header)
            pushChunk(self.gpa, &self.header, &self.hdr_pending, bytes)
        else
            self.append(bytes);
    }

    /// Feed captured output into the scrollback (one logical line per '\n'), then repaint.
    pub fn append(self: *Screen, bytes: []const u8) void {
        self.has_sel = false; // new output invalidates any highlight
        self.sel_active = false;
        const before = self.lines.items.len;
        pushChunk(self.gpa, &self.lines, &self.pending, bytes);
        self.wrapNewLines(before);
        // Cap the buffer, freeing the oldest lines.
        while (self.lines.items.len > max_lines) {
            self.gpa.free(self.lines.orderedRemove(0));
        }
        if (self.scroll_off == 0) {
            // The lines just added sweep in from the left; everything else lands at once.
            const grew = self.lines.items.len -| before;
            const skip = self.skip_reveal_once;
            self.skip_reveal_once = false; // one-shot, consumed whether it applied or not
            if (grew != 0 and !skip and logoFx.revealing(self))
                logoFx.revealNew(self, @min(grew, self.lines.items.len))
            else
                self.paintBody();
            self.drawStatusBar();
        } else {
            // Scrolled up: keep the viewport anchored on what the user is reading.
            const added = self.lines.items.len - @min(before, self.lines.items.len);
            self.scroll_off += added;
            self.clampScroll();
            self.drawStatusBar();
        }
    }

    /// Re-split lines just added that are wider than the window, so long output wraps onto as
    /// many rows as it needs. One scrollback line stays one screen row (keeps scrolling and
    /// selection simple); already-wrapped lines keep their wrap width on resize.
    fn wrapNewLines(self: *Screen, from: usize) void {
        if (self.cols == 0) return;
        var i = from;
        while (i < self.lines.items.len) : (i += 1) {
            const cut = ansi.splitAt(self.lines.items[i], self.cols);
            if (cut >= self.lines.items[i].len) continue; // fits
            const line = self.lines.items[i];
            const head = self.gpa.dupe(u8, line[0..cut]) catch continue;
            const tail = self.gpa.dupe(u8, line[cut..]) catch {
                self.gpa.free(head);
                continue;
            };
            self.lines.insert(self.gpa, i + 1, tail) catch {
                self.gpa.free(head);
                self.gpa.free(tail);
                continue;
            };
            self.lines.items[i] = head;
            self.gpa.free(line);
            // The tail is re-examined on the next turn of the loop, so a very long line keeps
            // splitting until every piece fits.
        }
    }

    /// Swap scrollback line `idx` for `bytes` in place — a transient notice advancing a
    /// frame. Only while that line still reads `expect`: output since then may have moved
    /// or wrapped it, and then nothing happens. True when it was swapped.
    pub fn replaceLine(self: *Screen, idx: usize, expect: []const u8, bytes: []const u8) bool {
        if (!self.lineIs(idx, expect)) return false;
        const copy = self.gpa.dupe(u8, bytes) catch return false;
        self.gpa.free(self.lines.items[idx]);
        self.lines.items[idx] = copy;
        self.paintBody();
        return true;
    }

    /// Drop scrollback line `idx` — a transient notice whose wait is over — under the same
    /// guard as replaceLine. True when it went.
    pub fn removeLine(self: *Screen, idx: usize, expect: []const u8) bool {
        if (!self.lineIs(idx, expect)) return false;
        self.gpa.free(self.lines.orderedRemove(idx));
        self.has_sel = false; // the rows below moved up, so a highlight no longer marks its text
        self.sel_active = false;
        self.clampScroll();
        self.paintBody();
        self.drawStatusBar();
        return true;
    }

    fn lineIs(self: *Screen, idx: usize, expect: []const u8) bool {
        return idx < self.lines.items.len and std.mem.eql(u8, self.lines.items[idx], expect);
    }

    /// Drop all scrollback (the `/clear` command) and repaint an empty body.
    pub fn clearScrollback(self: *Screen) void {
        for (self.lines.items) |l| self.gpa.free(l);
        self.lines.clearRetainingCapacity();
        self.pending.clearRetainingCapacity();
        self.scroll_off = 0;
        self.paintBody();
        self.drawStatusBar();
    }

    // ── scrolling ────────────────────────────────────────────────────────────────

    fn maxScroll(self: *Screen) usize {
        const bh = self.bodyRows();
        const n = self.lines.items.len;
        return if (n > bh) n - bh else 0;
    }
    pub fn clampScroll(self: *Screen) void {
        const m = self.maxScroll();
        if (self.scroll_off > m) self.scroll_off = m;
    }

    /// Wheel/PageUp/PageDown/End. `up` older, `!up` newer; `page` uses a viewport-sized step.
    pub fn scroll(self: *Screen, up: bool, page: bool) void {
        // The highlight is keyed to screen rows, so scrolling would leave it over different text —
        // drop it first.
        self.has_sel = false;
        self.sel_active = false;
        const step: usize = if (page) @max(1, self.bodyRows()) else wheel_step;
        if (up) {
            self.scroll_off = @min(self.scroll_off + step, self.maxScroll());
        } else {
            self.scroll_off -= @min(step, self.scroll_off);
        }
        self.paintBody();
        self.drawStatusBar();
    }
    // ── painting ──────────────────────────────────────────────────────────────────

    fn fullPaint(self: *Screen) void {
        ttyWrite(self.fd, "\x1b[2J"); // full clear — only for the initial paint / a resize
        self.repaint();
    }

    // Repaint every region in place (per-line clears, no full-screen \x1b[2J) — used on a theme
    // change so the recolour doesn't flash the whole screen.
    fn repaint(self: *Screen) void {
        self.paintHeader();
        self.paintBody();
        self.drawStatusBar();
    }

    pub fn paintHeader(self: *Screen) void {
        var rb: [8192]u8 = undefined;
        for (self.header.items, 0..) |line, i| {
            gotoRow(self.fd, @intCast(i + 1));
            ttyWrite(self.fd, ansi.clip(line, self.cols, &rb));
            ttyWrite(self.fd, "\x1b[K"); // erase to end IN PLACE — no clear-then-draw blank flash
        }
    }

    /// Repaint everything a highlight can cover: the output body and the input block. The
    /// editor owns the input rows normally, so they are only redrawn here while a selection is
    /// on them — its next refresh (any keystroke) takes them back.
    fn repaintSelection(self: *Screen) void {
        self.paintBody();
        self.paintPromptSelection();
    }

    /// Test seams: build a selection and tear the screen down without a live terminal.
    pub fn selectForTest(self: *Screen, ar: u16, ac: u16, hr: u16, hc: u16) void {
        self.sel_ar = ar;
        self.sel_ac = ac;
        self.sel_hr = hr;
        self.sel_hc = hc;
        self.has_sel = true;
        self.extractSelection(); // what a finished drag leaves behind, ready for Ctrl-C/Ctrl-S
    }

    /// Stand in as the current screen and the output sink without a terminal (fd -1), so a
    /// test can drive what `logo.print` lands in the scrollback.
    pub fn installForTest(self: *Screen) void {
        logo.setSink(sinkTrampoline, self);
        g_screen = self;
    }

    pub fn uninstallForTest(self: *Screen) void {
        if (g_screen == self) g_screen = null;
        logo.clearSink();
    }

    pub fn freeAllForTest(self: *Screen) void {
        self.freeAll();
    }

    /// Whether a highlight is on screen at all — the editor checks before asking for the input
    /// rows to be re-washed after it repaints them.
    pub fn hasHighlight(self: *Screen) bool {
        return self.has_sel;
    }

    /// Draw the input rows with the selection wash over them (or plain, to clear it).
    pub fn paintPromptSelection(self: *Screen) void {
        var rb: [8192]u8 = undefined;
        for (self.prompt_lines.items, 0..) |line, i| {
            const row: u16 = self.promptRow() + @as(u16, @intCast(i));
            if (row > self.rows) break;
            gotoRow(self.fd, row);
            if (self.selRowCols(row)) |sel|
                ttyWrite(self.fd, ansi.clipHighlight(line, self.cols, sel.c0, sel.c1, &rb))
            else
                ttyWrite(self.fd, ansi.clip(line, self.cols, &rb));
            ttyWrite(self.fd, "\x1b[K");
        }
    }

    pub fn paintBody(self: *Screen) void {
        self.paintBodyCut(null, 0);
    }

    // `paintBody`, with the rows from scrollback index `reveal_from` on drawn only as far as
    // their first `x` visible columns (`null` = the normal, whole-line paint).
    pub fn paintBodyCut(self: *Screen, reveal_from: ?usize, x: u16) void {
        if (self.bodyRows() == 0) return;
        self.clampScroll();
        const w = self.window();
        // Top-aligned below the logo; each row is OVERWRITTEN in place (write + erase-to-EOL),
        // never cleared-then-drawn, so repainting unchanged text produces no blank flash.
        var rb: [8192]u8 = undefined;
        var r: u16 = self.headerRows() + 1;
        var i: usize = w.first;
        while (i < w.end) : (i += 1) {
            gotoRow(self.fd, r);
            const cut = if (reveal_from) |f| i >= f else false;
            if (cut) // an arriving row, drawn only as far as the sweep has come
                ttyWrite(self.fd, ansi.clipPrefix(self.lines.items[i], self.cols, x, &rb))
            else if (self.selRowCols(r)) |sel| // this row is (partly) selected → draw it highlighted
                ttyWrite(self.fd, ansi.clipHighlight(self.lines.items[i], self.cols, sel.c0, sel.c1, &rb))
            else
                ttyWrite(self.fd, ansi.clip(self.lines.items[i], self.cols, &rb));
            ttyWrite(self.fd, "\x1b[K");
            r += 1;
        }
        // Clear the gap between the last output row and the pinned rule (wipes old output on a
        // shrink); the rule and prompt rows are owned by drawStatusBar and the line editor.
        while (r <= self.statusRow() -| 1) : (r += 1) gotoClear(self.fd, r);
    }

    // A single thin full-width accent-coloured line separating the scrollback from the prompt. When
    // scrolled up it carries a small dim right-aligned scroll indicator; otherwise it's a clean line.
    fn drawStatusBar(self: *Screen) void {
        self.drawStatusBarWipe(self.cols, "");
    }

    // The rule, mid-recolour: its first `x` columns in the live accent and the rest in
    // `old_accent` (empty = the whole rule is live, the normal draw).
    pub fn drawStatusBarWipe(self: *Screen, x: u16, old_accent: []const u8) void {
        if (self.rows < 2) return;
        var buf: [8192]u8 = undefined;
        const color = logo.colorEnabled();
        var rbuf: [64]u8 = undefined;
        // Only a functional scroll indicator is shown — the how-to hints (select / copy / theme) are gone.
        const hint: []const u8 = if (self.scroll_off != 0)
            (std.fmt.bufPrint(&rbuf, " \u{2191} scrolled {d} · PgDn resumes ", .{self.scroll_off}) catch "")
        else
            "";
        const cols: usize = self.cols;
        const hint_cols = ansi.visColumns(hint);
        const rule_cols = if (cols > hint_cols) cols - hint_cols else 0;
        var fb = std.Io.Writer.fixed(&buf);
        _ = fb.print("\x1b[{d};1H\x1b[2K", .{self.statusRow()}) catch {};
        // accentReal(), not accentSeq(): written straight to the terminal, so it needs the real SGR
        // escape — the sentinel is only for scrollback text that later passes through ansi.clip().
        if (color) _ = fb.writeAll(logo.accentReal()) catch {};
        var k: usize = 0;
        while (k < rule_cols) : (k += 1) {
            // Hand over to the outgoing accent where the wipe has not reached yet.
            if (color and old_accent.len != 0 and k == @as(usize, x)) _ = fb.writeAll(old_accent) catch {};
            _ = fb.writeAll("\u{2501}") catch {}; // ━ heavy horizontal (one thin line)
        }
        if (color) _ = fb.writeAll("\x1b[0m\x1b[2m") catch {}; // dim scroll indicator
        _ = fb.writeAll(hint) catch {};
        if (color) _ = fb.writeAll("\x1b[0m") catch {};
        ttyWrite(self.fd, fb.buffered());
    }
};

// ── free helpers (pure, unit-tested) ─────────────────────────────────────────────

/// libc write to a raw fd (std.posix.write is unavailable here the same way line_edit uses).
pub fn ttyWrite(fd: std.posix.fd_t, bytes: []const u8) void {
    var i: usize = 0;
    while (i < bytes.len) {
        const n = std.c.write(fd, bytes[i..].ptr, bytes.len - i);
        if (n <= 0) return;
        i += @intCast(n);
    }
}

/// Move the cursor to (row,1) without clearing — for in-place overwrites during the animation.
pub fn gotoRow(fd: std.posix.fd_t, row: u16) void {
    var b: [16]u8 = undefined;
    const s = std.fmt.bufPrint(&b, "\x1b[{d};1H", .{row}) catch return;
    ttyWrite(fd, s);
}

/// Move the cursor to (row,1) and clear the whole line.
fn gotoClear(fd: std.posix.fd_t, row: u16) void {
    var b: [24]u8 = undefined;
    const s = std.fmt.bufPrint(&b, "\x1b[{d};1H\x1b[2K", .{row}) catch return;
    ttyWrite(fd, s);
}

/// Append `bytes` to `pending`, flushing a completed owned line into `dst` on each '\n'
/// (the trailing '\r' of a CRLF is dropped). Allocation failures silently drop the line.
fn pushChunk(gpa: std.mem.Allocator, dst: *std.ArrayList([]u8), pending: *std.ArrayList(u8), bytes: []const u8) void {
    for (bytes) |ch| {
        if (ch == '\n') {
            var line = pending.items;
            if (line.len != 0 and line[line.len - 1] == '\r') line = line[0 .. line.len - 1];
            const owned = gpa.dupe(u8, line) catch {
                pending.clearRetainingCapacity();
                continue;
            };
            dst.append(gpa, owned) catch gpa.free(owned);
            pending.clearRetainingCapacity();
        } else {
            pending.append(gpa, ch) catch {};
        }
    }
}

/// Parse a `/reveal-speed` value: a number on the 0.01 … 1 scale, where 1 means "instantly" and
/// smaller is slower. Null for anything unparseable or out of range — 0 included, since a
/// speed of zero is an animation that never ends; 0.01 is as slow as the scale goes.
pub fn parseRevealSpeed(text: []const u8) ?f64 {
    const v = std.fmt.parseFloat(f64, std.mem.trim(u8, text, " \t")) catch return null;
    if (std.math.isNan(v) or v < reveal_speed_min or v > reveal_speed_max) return null;
    return v;
}

/// The scale's ends and default, for the `/reveal-speed` command's messages.
pub const speed_min = reveal_speed_min;
pub const speed_max = reveal_speed_max;
pub const speed_default = reveal_speed_default;

// ── mouse parsing ────────────────────────────────────────────────────────────────

pub const Mouse = struct {
    btn: u16, // full SGR button code: low 2 bits = button, +64 = wheel, higher bits = modifiers
    col: u16, // 1-based
    row: u16, // 1-based
    press: bool, // true = press ('M'), false = release ('m')

    pub fn isWheelUp(m: Mouse) bool {
        return (m.btn & 64) != 0 and (m.btn & 1) == 0;
    }
    pub fn isWheelDown(m: Mouse) bool {
        return (m.btn & 64) != 0 and (m.btn & 1) == 1;
    }
    // A wheel notch has bit 64; drag-motion has bit 32. A plain left-button press is neither.
    pub fn isLeftPress(m: Mouse) bool {
        return m.press and (m.btn & 64) == 0 and (m.btn & 32) == 0 and (m.btn & 3) == 0;
    }
    // Motion while the left button is held (SGR sets bit 32 on drag reports) — a text drag.
    pub fn isLeftDrag(m: Mouse) bool {
        return m.press and (m.btn & 32) != 0 and (m.btn & 64) == 0 and (m.btn & 3) == 0;
    }
    pub fn isRelease(m: Mouse) bool {
        return !m.press;
    }
};

/// Parse the body of an SGR mouse report — the bytes after the `ESC [ <` intro, including the
/// terminating 'M' (press) or 'm' (release): `btn ; col ; row (M|m)`. Returns null on garbage.
pub fn parseMouse(seq: []const u8) ?Mouse {
    if (seq.len < 6) return null;
    const last = seq[seq.len - 1];
    if (last != 'M' and last != 'm') return null;
    var it = std.mem.splitScalar(u8, seq[0 .. seq.len - 1], ';');
    const btn = std.fmt.parseInt(u16, it.next() orelse return null, 10) catch return null;
    const col = std.fmt.parseInt(u16, it.next() orelse return null, 10) catch return null;
    const row = std.fmt.parseInt(u16, it.next() orelse return null, 10) catch return null;
    if (it.next() != null) return null;
    return .{ .btn = btn, .col = col, .row = row, .press = last == 'M' };
}

// ── accent cycle (mirrors browser/js/ui/toolbar.js cycleAccent) ───────────────────

const theme = @import("../theme.zig");

/// The next accent key when the logo is single-clicked: advance through the preset list
/// (wrapping), or reset to the default when a custom colour (`current` is a '#hex') is active
/// — exactly what the browser does. `current` is the active accent key.
pub fn nextAccentKey(cur: []const u8) []const u8 {
    if (cur.len != 0 and cur[0] == '#') return theme.default_key;
    var idx: usize = 0;
    const all = theme.accents();
    for (all, 0..) |a, i| {
        if (std.ascii.eqlIgnoreCase(a.key, cur)) {
            idx = i;
            break;
        }
    }
    return all[(idx + 1) % all.len].key;
}

// ── tests (pure helpers only; the terminal path never runs in CI) ─────────────────

const testing = std.testing;

test "pushChunk: splits on newline, drops CR, buffers partials" {
    var lines: std.ArrayList([]u8) = .empty;
    var pending: std.ArrayList(u8) = .empty;
    defer {
        for (lines.items) |l| testing.allocator.free(l);
        lines.deinit(testing.allocator);
        pending.deinit(testing.allocator);
    }
    pushChunk(testing.allocator, &lines, &pending, "one\r\ntwo\n");
    pushChunk(testing.allocator, &lines, &pending, "par");
    pushChunk(testing.allocator, &lines, &pending, "tial\n");
    try testing.expectEqual(@as(usize, 3), lines.items.len);
    try testing.expectEqualStrings("one", lines.items[0]);
    try testing.expectEqualStrings("two", lines.items[1]);
    try testing.expectEqualStrings("partial", lines.items[2]);
}

test "parseMouse: SGR press/release, wheel classification" {
    const p = parseMouse("0;10;3M").?;
    try testing.expect(p.press and p.isLeftPress());
    try testing.expectEqual(@as(u16, 10), p.col);
    try testing.expectEqual(@as(u16, 3), p.row);
    try testing.expect(parseMouse("0;10;3m").?.press == false);
    try testing.expect(parseMouse("64;5;5M").?.isWheelUp());
    try testing.expect(parseMouse("65;5;5M").?.isWheelDown());
    try testing.expect(parseMouse("garbage") == null);
    try testing.expect(parseMouse("1;2X") == null);
}

test "nextAccentKey: advances presets, wraps, resets from custom" {
    try testing.expectEqualStrings("pink", nextAccentKey("violet")); // first -> second
    try testing.expectEqualStrings("violet", nextAccentKey("grey")); // last wraps to first
    try testing.expectEqualStrings("violet", nextAccentKey("#ff8800")); // custom -> default
    try testing.expectEqualStrings("pink", nextAccentKey("VIOLET")); // case-insensitive
}

test "a speed change forgets the burst, so its own confirmation runs at the NEW speed" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var s = Screen{ .gpa = a, .io = threaded.io(), .fd = -1, .rows = 10, .cols = 40 };
    defer s.freeAll();
    // Mid-burst: output has been flowing, so the next line would take the abbreviated form.
    s.reveal_last_ns = 1234;
    s.reveal_burst_ns = 1000;
    s.setRevealSpeed(0.05);
    // The confirmation line arrives right behind the echo of the command that set the speed —
    // if it inherited that burst it would be the one line NOT running at the speed it names.
    try testing.expectEqual(@as(i96, 0), s.reveal_last_ns);
    try testing.expectEqual(@as(i96, 0), s.reveal_burst_ns);
}

test "skipRevealOnce: the echo of a typed line lands at once, and only that line" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var s = Screen{ .gpa = a, .io = threaded.io(), .fd = -1, .rows = 10, .cols = 40 };
    defer s.freeAll();
    try testing.expect(!s.skip_reveal_once);
    s.skipRevealOnce(); // console.zig arms this around the command echo
    try testing.expect(s.skip_reveal_once);
    s.append("> /reveal-speed 0.05\n");
    // One-shot: consumed by that append, so the command's real output still sweeps in.
    try testing.expect(!s.skip_reveal_once);
    try testing.expectEqual(@as(usize, 1), s.lines.items.len);
    // Armed but nothing arrives (an empty line): still consumed, never left standing.
    s.skipRevealOnce();
    s.append("");
    try testing.expect(!s.skip_reveal_once);
}

test "parseRevealSpeed: the 0.01 … 1 scale, and what is not on it" {
    try testing.expectEqual(@as(f64, 0.5), parseRevealSpeed("0.5").?);
    try testing.expectEqual(@as(f64, 1.0), parseRevealSpeed("1").?);
    try testing.expectEqual(@as(f64, 0.01), parseRevealSpeed("0.01").?);
    try testing.expectEqual(@as(f64, 0.25), parseRevealSpeed(" .25 ").?); // padded, leading dot
    try testing.expect(parseRevealSpeed("0") == null); // never finishes — 0.01 is the floor
    try testing.expect(parseRevealSpeed("0.009") == null);
    try testing.expect(parseRevealSpeed("1.5") == null); // 1 is already instant
    try testing.expect(parseRevealSpeed("-1") == null);
    try testing.expect(parseRevealSpeed("fast") == null);
    try testing.expect(parseRevealSpeed("") == null);
    try testing.expect(parseRevealSpeed("nan") == null);
}

test "selection covers the INPUT rows too, not just the output above them" {
    const a = testing.allocator;
    // A screen with no tty behind it: nothing here paints, only the selection model runs.
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var s = Screen{ .gpa = a, .io = threaded.io(), .fd = -1, .rows = 10, .cols = 40 };
    defer s.freeAll();
    try s.lines.append(a, try a.dupe(u8, "wrote out.png"));
    try s.header.append(a, try a.dupe(u8, "  logo"));
    s.setPromptText(&.{"> /theme hello world"});

    // The input block is the bottom row; the header is not selectable, the body and the
    // input are.
    try testing.expect(!s.selectableRow(1)); // logo header
    try testing.expect(s.selectableRow(s.bodyTop()));
    try testing.expect(s.selectableRow(s.promptRow()));

    // Drag across "hello" on the input row and take it: the typed line is what comes out.
    const row = s.promptRow();
    s.sel_ar = row;
    s.sel_ac = 10; // 1-based columns: "> /theme |hello world"
    s.sel_hr = row;
    s.sel_hc = 14;
    s.has_sel = true;
    s.extractSelection();
    try testing.expectEqualStrings("hello", s.sel_buf.items);

    // A drag that starts in the output and ends on the input carries both lines.
    s.sel_ar = s.bodyTop();
    s.sel_ac = 1;
    s.sel_hr = row;
    s.sel_hc = 8;
    s.extractSelection();
    try testing.expectEqualStrings("wrote out.png\n> /theme", s.sel_buf.items);
}

test "replaceLine / removeLine: only the line that still reads as expected is touched" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    var s = Screen{ .gpa = a, .io = threaded.io(), .fd = -1, .rows = 10, .cols = 40 };
    defer s.freeAll();
    s.setRevealSpeed(1.0);
    s.append("one\n◐ wait\nthree\n");
    try testing.expect(s.replaceLine(1, "◐ wait", "◓ wait"));
    try testing.expectEqualStrings("◓ wait", s.lines.items[1]);
    // Stale expectation (the frame already moved on) or a bad index: untouched.
    try testing.expect(!s.replaceLine(1, "◐ wait", "◑ wait"));
    try testing.expect(!s.replaceLine(7, "◓ wait", "◑ wait"));
    try testing.expectEqualStrings("◓ wait", s.lines.items[1]);
    try testing.expect(!s.removeLine(1, "◐ wait"));
    try testing.expectEqual(@as(usize, 3), s.lines.items.len);
    try testing.expect(s.removeLine(1, "◓ wait"));
    try testing.expectEqual(@as(usize, 2), s.lines.items.len);
    try testing.expectEqualStrings("one", s.lines.items[0]);
    try testing.expectEqualStrings("three", s.lines.items[1]);
}

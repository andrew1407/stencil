//! Full-screen ("TUI") console renderer for the interactive REPL. When stdin is a TTY and
//! the terminal is tall enough, `console.zig` runs the editor inside this screen: the logo
//! banner is pinned as a fixed header at the top (echoing the browser's clickable app logo),
//! every line of human output is captured into an in-memory scrollback buffer and drawn into
//! a body viewport that the mouse wheel / PageUp / PageDown can scroll back through, a status
//! bar sits above the prompt, and the prompt itself owns the bottom row.
//!
//! Mouse tracking (SGR 1006) is enabled so a click on the pinned logo cycles the accent and a
//! double-click drops the user into a custom-colour entry — the terminal-native equivalent of
//! the browser logo's single-click-cycle / double-click-colour-picker (browser/js/ui/toolbar.js).
//! Dragging over output paints a translucent theme-colour highlight; Ctrl-S copies it (Cmd-C can't
//! reach the app under mouse tracking), or `/mouse off` restores native selection.
//!
//! It leans on `logo.zig`'s output sink: while the screen is active every `logo.print` is
//! routed into `append`, so the many existing handlers keep printing exactly as before and
//! land in the scrollback instead of scrolling the raw terminal. Everything degrades to the
//! plain line editor when the terminal is too small or size detection fails, and it is never
//! reached by piped input or CI (that path never enters raw mode).
const std = @import("std");
const logo = @import("../logo.zig");

// One active screen at a time; handlers reach it (for a theme repaint) via `current()`.
var g_screen: ?*Screen = null;
pub fn current() ?*Screen {
    return g_screen;
}

const max_lines = 5000; // scrollback cap; oldest lines drop past this
const wheel_step = 3; // rows per wheel notch
const header_pad = 1; // blank rows between the logo header and the output
const wordmark = "S T E N C I L"; // the logo wordmark, animated on a theme change
const flourish_step_ms = 73; // per-letter pace of the theme-change wordmark wave

pub const Screen = struct {
    gpa: std.mem.Allocator,
    io: std.Io,
    fd: std.posix.fd_t = std.posix.STDERR_FILENO,
    in_fd: ?std.posix.fd_t = null, // input tty fd — polled to abort the flourish when a click is queued

    rows: u16 = 24,
    cols: u16 = 80,
    header: std.ArrayList([]u8) = .empty, // pinned logo lines (owned)
    lines: std.ArrayList([]u8) = .empty, // scrollback (owned)
    pending: std.ArrayList(u8) = .empty, // partial line being accumulated
    hdr_pending: std.ArrayList(u8) = .empty, // partial header line (during capture)
    capturing_header: bool = false,
    scroll_off: usize = 0, // lines scrolled up from the live bottom (0 = live)
    mouse_on: bool = false, // SGR mouse reporting state (toggled by /mouse)
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

    /// Enter full-screen mode: measure the terminal, capture the logo header, switch to the
    /// alternate screen, enable mouse reporting, install the output sink and paint once. On
    /// any failure it tears down cleanly and returns an error so the caller falls back to the
    /// plain editor. `self` must have a stable address for the session (its pointer is handed
    /// to the sink and to `g_screen`).
    pub fn start(self: *Screen) Error!void {
        try self.querySize();
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
        self.setMouse(true); // SGR mouse reporting (1000 = click, 1006 = extended coordinates)
        logo.setAccentSentinel(true); // stored accent spans re-tint to the live accent on repaint
        g_screen = self;
        self.fullPaint();
    }

    pub fn deinit(self: *Screen) void {
        logo.clearSink();
        logo.setAccentSentinel(false);
        self.setMouse(false);
        // Restore: re-enable autowrap, leave the alternate screen.
        ttyWrite(self.fd, "\x1b[?7h\x1b[?1049l");
        g_screen = null;
        self.freeAll();
    }

    pub fn mouseOn(self: *Screen) bool {
        return self.mouse_on;
    }

    /// Enable/disable SGR mouse reporting. Off hands the mouse back to the terminal so the user
    /// can select/copy text natively (mouse tracking otherwise suppresses native selection).
    pub fn setMouse(self: *Screen, on: bool) void {
        if (on and self.mouse_on) return; // already on (a disable always re-emits, for safe teardown)
        self.mouse_on = on;
        // 1002 = button + drag-motion reporting (needed for the drag-to-highlight visual), 1006 = SGR coords.
        ttyWrite(self.fd, if (on) "\x1b[?1002h\x1b[?1006h" else "\x1b[?1002l\x1b[?1006l");
        if (g_screen == self) self.drawStatusBar(); // reflect the state in the rule hint
    }

    fn freeAll(self: *Screen) void {
        for (self.header.items) |l| self.gpa.free(l);
        self.header.deinit(self.gpa);
        for (self.lines.items) |l| self.gpa.free(l);
        self.lines.deinit(self.gpa);
        self.pending.deinit(self.gpa);
        self.hdr_pending.deinit(self.gpa);
        self.sel_buf.deinit(self.gpa);
    }

    // ── geometry ────────────────────────────────────────────────────────────────

    fn headerRows(self: *Screen) u16 {
        return @intCast(self.header.items.len);
    }
    /// Max rows available for content (between the header and the rule + prompt at the bottom).
    fn bodyRows(self: *Screen) u16 {
        const used = self.headerRows() + 2; // 1 rule row + 1 prompt row
        return if (self.rows > used) self.rows - used else 0;
    }
    // The half-open slice [first,end) of `lines` that the body viewport currently shows, honouring
    // the scroll offset. The single source of truth for every paint/extract loop.
    const Window = struct { first: usize, end: usize };
    fn window(self: *Screen) Window {
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
    // The prompt owns the terminal's bottom row and the rule (a single thin line) sits just above
    // it — both PINNED to the bottom of the screen (not floating up under the output), so in a tall
    // window the input is always where a full-screen app puts it. Output fills the gap between the
    // header and the rule (top-aligned, growing downward). start() guarantees rows >= headerRows()+4.
    fn statusRow(self: *Screen) u16 {
        return self.rows - 1;
    }
    pub fn promptRow(self: *Screen) u16 {
        return self.rows;
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
        if (row < self.bodyTop() or row > self.bodyBottom()) {
            if (had) self.paintBody(); // clear a stale highlight
            return;
        }
        self.sel_active = true;
        self.sel_ar = row;
        self.sel_ac = col;
        self.sel_hr = row;
        self.sel_hc = col;
        if (had) self.paintBody(); // clear a stale highlight
    }

    /// Extend the in-progress selection to (col,row) and repaint the live highlight.
    pub fn selDrag(self: *Screen, col: u16, row: u16) void {
        if (!self.sel_active) return;
        const top = self.bodyTop();
        const bot = @max(top, self.bodyBottom());
        self.sel_hr = std.math.clamp(row, top, bot);
        self.sel_hc = @max(@as(u16, 1), @min(col, self.cols));
        self.has_sel = true;
        self.paintBody();
    }

    /// Finish the drag: extract the highlighted text into `sel_buf` and KEEP the highlight on
    /// screen. Nothing is copied until Ctrl-S. No-op for a plain click (no drag).
    pub fn selEnd(self: *Screen) void {
        if (!self.sel_active) return;
        self.sel_active = false;
        if (!self.has_sel) return; // a click without a drag selects nothing
        self.extractSelection();
        self.paintBody(); // settle the final highlight (drag is over)
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
        self.paintBody();
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
        const w = self.window();
        const first = w.first;
        const end = w.end;
        const s = self.selNorm();
        var row: u16 = s.sr;
        while (row <= s.er) : (row += 1) {
            const idx = first + (row - self.bodyTop());
            if (idx >= end) break;
            const c0: u16 = if (row == s.sr) s.sc - 1 else 0;
            const c1: u16 = if (row == s.er) s.ec else self.cols;
            var seg: [8192]u8 = undefined;
            const text = visibleSlice(self.lines.items[idx], c0, c1, &seg);
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

    fn captureHeader(self: *Screen) void {
        for (self.header.items) |l| self.gpa.free(l);
        self.header.clearRetainingCapacity();
        self.hdr_pending.clearRetainingCapacity();
        self.capturing_header = true;
        logo.banner();
        self.capturing_header = false;
        // Drop the blank leading/trailing lines banner() emits so the header hugs the top.
        while (self.header.items.len != 0 and self.header.items[0].len == 0) {
            self.gpa.free(self.header.orderedRemove(0));
        }
        while (self.header.items.len != 0 and self.header.items[self.header.items.len - 1].len == 0) {
            self.gpa.free(self.header.pop().?);
        }
        // Locate the "S T E N C I L" wordmark (row + starting visible column) for the
        // theme-change animation, before we append the padding rows.
        self.wordmark_row = 0;
        for (self.header.items, 0..) |line, idx| {
            if (std.mem.indexOf(u8, line, wordmark)) |off| {
                self.wordmark_row = @intCast(idx + 1); // 1-based screen row
                self.wordmark_col = @intCast(visColumns(line[0..off]) + 1);
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

    /// Recapture the header in the new accent, recolour everything in place, then play the logo
    /// animation — called after a `/theme` change or a logo click.
    pub fn onThemeChanged(self: *Screen) void {
        self.captureHeader();
        self.recolorRepaint();
        self.animateThemeChange();
    }

    // Repaint header + body + rule by OVERWRITING each line in place (no per-line clear). A
    // recolour produces identical text at identical positions, so overwriting swaps only the
    // colour with no intermediate blank — eliminating the flicker a clear-then-draw would cause.
    fn recolorRepaint(self: *Screen) void {
        var rb: [8192]u8 = undefined;
        for (self.header.items, 0..) |line, i| {
            gotoRow(self.fd, @intCast(i + 1));
            ttyWrite(self.fd, clip(line, self.cols, &rb));
        }
        if (self.bodyRows() != 0) {
            self.clampScroll();
            const w = self.window();
            var r: u16 = self.headerRows() + 1;
            var i: usize = w.first;
            while (i < w.end) : (i += 1) {
                gotoRow(self.fd, r);
                ttyWrite(self.fd, clip(self.lines.items[i], self.cols, &rb));
                r += 1;
            }
        }
        self.drawStatusBar();
    }

    // The flourish when the accent changes: the "S T E N C I L" wordmark lights up one letter at a
    // time in the new accent. The accent is already applied (recolorRepaint ran first), so this is
    // purely cosmetic — a queued keystroke/click aborts it mid-pass so a click-storm coalesces to
    // the final accent instead of replaying an animation per click.
    fn animateThemeChange(self: *Screen) void {
        if (!logo.colorEnabled()) return;
        const row = self.wordmark_row;
        if (row == 0) return;
        const accent = logo.accentReal(); // the real escape, never the sentinel (this bypasses clip)
        const rst = "\x1b[0m";
        // Letter cells within the 13-char wordmark ("S T E N C I L"): 0,2,4,6,8,10,12 — one
        // travelling pass. The lit letter turns bold in the NEW accent while every other letter
        // stays in its ORIGINAL colour (normal bold default) — never dimmed, so the wordmark keeps
        // its usual look and only the moving letter changes colour, settling back to normal after.
        const positions = [_]usize{ 0, 2, 4, 6, 8, 10, 12 };
        for (0..positions.len) |hi| {
            var buf: [256]u8 = undefined;
            var fb = std.Io.Writer.fixed(&buf);
            _ = fb.print("\x1b[{d};{d}H", .{ row, self.wordmark_col }) catch {};
            for (wordmark, 0..) |ch, ci| {
                _ = fb.writeAll("\x1b[1m") catch {}; // bold, like the normal wordmark
                if (positions[hi] == ci) _ = fb.writeAll(accent) catch {}; // active letter → accent
                _ = fb.writeByte(ch) catch {};
                _ = fb.writeAll(rst) catch {};
            }
            ttyWrite(self.fd, fb.buffered());
            // Bail the moment a newer click/keystroke is waiting — it supersedes this flourish.
            if (self.sleepOrAbort(flourish_step_ms)) break;
        }
        // Settle: redraw the wordmark row from the captured header (bold default wordmark). This
        // runs whether the loop finished or aborted early, so the wordmark never stalls half-lit.
        if (row <= self.header.items.len) {
            var rb: [8192]u8 = undefined;
            gotoRow(self.fd, row);
            ttyWrite(self.fd, clip(self.header.items[row - 1], self.cols, &rb));
        }
    }

    // Busy-wait ~`ms` against the monotonic clock (a plain nanosleep is coalesced/interrupted by the
    // io event loop, which breaks frame pacing), but return true early the instant a byte is waiting
    // on the input tty — a queued click/keystroke that should supersede the flourish. Flourish-only,
    // so the brief CPU spin is harmless.
    fn sleepOrAbort(self: *Screen, ms: i64) bool {
        const deadline: i96 = std.Io.Clock.now(.awake, self.io).nanoseconds + @as(i96, ms) * std.time.ns_per_ms;
        while (std.Io.Clock.now(.awake, self.io).nanoseconds < deadline) {
            if (self.inputPending()) return true;
        }
        return false;
    }

    // Whether the input tty has a byte ready to read right now (non-blocking poll). False when no
    // input fd was wired up (e.g. tests) so the flourish just plays to completion.
    fn inputPending(self: *Screen) bool {
        const fd = self.in_fd orelse return false;
        var pfd = [_]std.posix.pollfd{.{ .fd = fd, .events = std.posix.POLL.IN, .revents = 0 }};
        const ready = std.posix.poll(&pfd, 0) catch return false;
        return ready > 0;
    }

    // ── output sink ──────────────────────────────────────────────────────────────

    fn sinkTrampoline(ctx: *anyopaque, bytes: []const u8) void {
        const self: *Screen = @ptrCast(@alignCast(ctx));
        if (self.capturing_header)
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
        // Cap the buffer, freeing the oldest lines.
        while (self.lines.items.len > max_lines) {
            self.gpa.free(self.lines.orderedRemove(0));
        }
        if (self.scroll_off == 0) {
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
    fn clampScroll(self: *Screen) void {
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

    fn paintHeader(self: *Screen) void {
        var rb: [8192]u8 = undefined;
        for (self.header.items, 0..) |line, i| {
            gotoRow(self.fd, @intCast(i + 1));
            ttyWrite(self.fd, clip(line, self.cols, &rb));
            ttyWrite(self.fd, "\x1b[K"); // erase to end IN PLACE — no clear-then-draw blank flash
        }
    }


    fn paintBody(self: *Screen) void {
        if (self.bodyRows() == 0) return;
        self.clampScroll();
        const w = self.window();
        // Top-align: output starts just below the logo and grows downward; the rule + prompt (drawn
        // by their owners) float just under the last line. Each row is OVERWRITTEN in place (write +
        // erase-to-EOL), never cleared-then-drawn, so a repeated repaint of unchanged text produces
        // no blank flash — killing flicker on a burst of output and keeping a drag highlight steady.
        var rb: [8192]u8 = undefined;
        var r: u16 = self.headerRows() + 1;
        var i: usize = w.first;
        while (i < w.end) : (i += 1) {
            gotoRow(self.fd, r);
            if (self.selRowCols(r)) |sel| // this row is (partly) selected → draw it highlighted
                ttyWrite(self.fd, clipHighlight(self.lines.items[i], self.cols, sel.c0, sel.c1, &rb))
            else
                ttyWrite(self.fd, clip(self.lines.items[i], self.cols, &rb));
            ttyWrite(self.fd, "\x1b[K");
            r += 1;
        }
        // Clear the empty gap between the last output row and the pinned rule (rows-1). These rows
        // are normally already blank, so erasing them is invisible (no glyph change → no flicker);
        // on a shrink (e.g. /clear) it wipes the lines that used to hold output. The rule and prompt
        // rows themselves are owned by drawStatusBar and the line editor — never touched here.
        while (r <= self.statusRow() -| 1) : (r += 1) gotoClear(self.fd, r);
    }

    // A single thin full-width accent-coloured line separating the scrollback from the prompt. When
    // scrolled up it carries a small dim right-aligned scroll indicator; otherwise it's a clean line.
    fn drawStatusBar(self: *Screen) void {
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
        const hint_cols = visColumns(hint);
        const rule_cols = if (cols > hint_cols) cols - hint_cols else 0;
        var fb = std.Io.Writer.fixed(&buf);
        _ = fb.print("\x1b[{d};1H\x1b[2K", .{self.statusRow()}) catch {};
        // accentReal(), not accentSeq(): written straight to the terminal, so it needs the real SGR
        // escape — the sentinel is only for scrollback text that later passes through clip().
        if (color) _ = fb.writeAll(logo.accentReal()) catch {};
        var k: usize = 0;
        while (k < rule_cols) : (k += 1) _ = fb.writeAll("\u{2501}") catch {}; // ━ heavy horizontal (one thin line)
        if (color) _ = fb.writeAll("\x1b[0m\x1b[2m") catch {}; // dim scroll indicator
        _ = fb.writeAll(hint) catch {};
        if (color) _ = fb.writeAll("\x1b[0m") catch {};
        ttyWrite(self.fd, fb.buffered());
    }
};

/// Count visible columns of a string: one per UTF-8 codepoint, skipping CSI/SGR escape
/// sequences and the zero-width accent sentinel (0x01). Colour escapes take no screen width,
/// so they must NOT be counted — otherwise a column computed off a coloured line (e.g. the
/// wordmark's start column, off a header line full of SGR escapes) lands far to the right.
fn visColumns(s: []const u8) usize {
    var n: usize = 0;
    var i: usize = 0;
    while (i < s.len) {
        const b = s[i];
        if (b == 0x01) { // accent sentinel — zero width
            i += 1;
            continue;
        }
        const esc = csiLen(s, i);
        if (esc != 0) { // CSI escape — zero width
            i += esc;
            continue;
        }
        if ((b & 0xc0) != 0x80) n += 1; // count everything but UTF-8 continuation bytes
        i += 1;
    }
    return n;
}

fn appendBytes(out: []u8, oi: *usize, s: []const u8) void {
    if (oi.* + s.len > out.len) return;
    @memcpy(out[oi.*..][0..s.len], s);
    oi.* += s.len;
}

// Selection wash opacity: the accent is applied at this fraction over the (dark) terminal
// background, so the highlight reads as a translucent tint of the theme colour rather than a solid
// fill. Lower = more transparent.
const sel_alpha_pct = 55;

/// A background SGR that washes the current accent over the terminal background at `sel_alpha_pct`%
/// — the translucent theme-colour selection highlight. There is no grey floor, so a low accent
/// stays genuinely faint. Falls back to reverse video when colour is off. Written into `buf`.
fn selHighlightSeq(buf: []u8) []const u8 {
    if (!logo.colorEnabled()) return "\x1b[7m";
    const a = logo.accentRgb();
    const mix = [3]u8{
        @intCast(@as(u16, a[0]) * sel_alpha_pct / 100),
        @intCast(@as(u16, a[1]) * sel_alpha_pct / 100),
        @intCast(@as(u16, a[2]) * sel_alpha_pct / 100),
    };
    return std.fmt.bufPrint(buf, "\x1b[48;2;{d};{d};{d}m", .{ mix[0], mix[1], mix[2] }) catch "\x1b[7m";
}
fn selHighlightOff() []const u8 {
    return if (logo.colorEnabled()) "\x1b[49m" else "\x1b[27m"; // reset bg, or leave reverse video
}

/// Render `line` clipped to `cols`, tinting visible columns `[c0,c1)` with a translucent wash of
/// the theme accent — the text-selection highlight. Unlike a reverse-video span this keeps the
/// row's own foreground colours: the accent background is re-asserted after every escape so the
/// line's internal SGR resets don't cancel the wash mid-selection.
fn clipHighlight(line: []const u8, cols: u16, c0: u16, c1: u16, out: []u8) []const u8 {
    var hbuf: [24]u8 = undefined;
    const on = selHighlightSeq(&hbuf);
    const off = selHighlightOff();
    var oi: usize = 0;
    var vis: u16 = 0;
    var i: usize = 0;
    var span = false;
    while (i < line.len and vis < cols) {
        const b = line[i];
        if (b == 0x01) { // accent sentinel → the live accent fg escape (zero width)
            appendBytes(out, &oi, logo.accentReal());
            if (span) appendBytes(out, &oi, on); // re-assert the wash the fg escape may not touch
            i += 1;
            continue;
        }
        const esc = csiLen(line, i);
        if (esc != 0) { // colour escape — keep it
            appendBytes(out, &oi, line[i .. i + esc]);
            if (span) appendBytes(out, &oi, on); // re-assert the wash after any reset in the line
            i += esc;
            continue;
        }
        const want = vis >= c0 and vis < c1;
        if (want and !span) {
            appendBytes(out, &oi, on);
            span = true;
        } else if (!want and span) {
            appendBytes(out, &oi, off);
            span = false;
        }
        const clen = @min(utf8Len(b), line.len - i);
        if (oi + clen > out.len) break;
        @memcpy(out[oi..][0..clen], line[i..][0..clen]);
        oi += clen;
        i += clen;
        vis += 1;
    }
    if (span) appendBytes(out, &oi, off);
    appendBytes(out, &oi, "\x1b[0m");
    return out[0..oi];
}

/// The plain visible characters of `line` in visible-column range `[c0,c1)` (colours/sentinel
/// stripped) — used to build the clipboard text for a selection.
fn visibleSlice(line: []const u8, c0: u16, c1: u16, out: []u8) []const u8 {
    var oi: usize = 0;
    var vis: u16 = 0;
    var i: usize = 0;
    while (i < line.len) {
        const b = line[i];
        if (b == 0x01) {
            i += 1;
            continue;
        }
        const esc = csiLen(line, i);
        if (esc != 0) {
            i += esc;
            continue;
        }
        if (vis >= c1) break;
        const clen = @min(utf8Len(b), line.len - i);
        if (vis >= c0) {
            if (oi + clen > out.len) break;
            @memcpy(out[oi..][0..clen], line[i..][0..clen]);
            oi += clen;
        }
        i += clen;
        vis += 1;
    }
    return out[0..oi];
}

// ── free helpers (pure, unit-tested) ─────────────────────────────────────────────

/// libc write to a raw fd (std.posix.write is unavailable here the same way line_edit uses).
fn ttyWrite(fd: std.posix.fd_t, bytes: []const u8) void {
    var i: usize = 0;
    while (i < bytes.len) {
        const n = std.c.write(fd, bytes[i..].ptr, bytes.len - i);
        if (n <= 0) return;
        i += @intCast(n);
    }
}

/// Move the cursor to (row,1) without clearing — for in-place overwrites during the animation.
fn gotoRow(fd: std.posix.fd_t, row: u16) void {
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

/// Number of bytes in the UTF-8 codepoint that starts with lead byte `b`.
fn utf8Len(b: u8) usize {
    if (b < 0x80) return 1;
    if (b >= 0xf0) return 4;
    if (b >= 0xe0) return 3;
    if (b >= 0xc0) return 2;
    return 1; // stray continuation byte — treat as one
}

/// If `line[i..]` begins a CSI/SGR escape (`ESC [` … final byte 0x40..0x7e), return its length
/// in bytes; 0 otherwise. Every line-scanning helper uses this to pass escapes through as
/// zero-width. The final byte is included; an unterminated escape runs to end-of-line.
fn csiLen(line: []const u8, i: usize) usize {
    if (!(line[i] == 0x1b and i + 1 < line.len and line[i + 1] == '[')) return 0;
    var j = i + 2;
    while (j < line.len and !(line[j] >= 0x40 and line[j] <= 0x7e)) : (j += 1) {}
    if (j < line.len) j += 1; // include the final byte
    return j - i;
}

/// Clip a possibly-ANSI-coloured line to `cols` *visible* columns, copying SGR/CSI escapes
/// through verbatim (they take no width) and ending with a reset so colour never bleeds into
/// the next row. Written into `out`; returns the filled slice. Multi-byte UTF-8 counts as one
/// column. This keeps every body row exactly one terminal row tall so the fixed layout holds.
fn clip(line: []const u8, cols: u16, out: []u8) []const u8 {
    var oi: usize = 0;
    var vis: u16 = 0;
    var i: usize = 0;
    while (i < line.len) {
        const b = line[i];
        if (b == 0x01) { // accent sentinel → the live accent escape (zero visible width)
            appendBytes(out, &oi, logo.accentReal());
            i += 1;
            continue;
        }
        const esc = csiLen(line, i);
        if (esc != 0) { // colour/CSI escape — copied verbatim, no width
            appendBytes(out, &oi, line[i .. i + esc]);
            i += esc;
            continue;
        }
        if (vis >= cols) break;
        const clen = @min(utf8Len(b), line.len - i);
        if (oi + clen > out.len) break;
        @memcpy(out[oi..][0..clen], line[i..][0..clen]);
        oi += clen;
        i += clen;
        vis += 1;
    }
    if (logo.colorEnabled()) appendBytes(out, &oi, "\x1b[0m"); // never bleed colour into the next row
    return out[0..oi];
}

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
    for (theme.accents, 0..) |a, i| {
        if (std.ascii.eqlIgnoreCase(a.key, cur)) {
            idx = i;
            break;
        }
    }
    return theme.accents[(idx + 1) % theme.accents.len].key;
}

// ── tests (pure helpers only; the terminal path never runs in CI) ─────────────────

const testing = std.testing;

test "clip: counts visible columns, passes ANSI through, appends reset" {
    logo.init(false); // colour on
    var out: [256]u8 = undefined;
    // Plain text clipped to 3 columns keeps 3 chars + reset.
    const a = clip("hello world", 3, &out);
    try testing.expectEqualStrings("hel\x1b[0m", a);
    // A colour escape is copied verbatim and does not consume width.
    const b = clip("\x1b[31mhi\x1b[0m", 10, &out);
    try testing.expectEqualStrings("\x1b[31mhi\x1b[0m\x1b[0m", b);
    // Multi-byte glyphs count as one column each (● is 3 bytes).
    const c = clip("\u{25cf}\u{25cf}\u{25cf}\u{25cf}", 2, &out);
    try testing.expectEqualStrings("\u{25cf}\u{25cf}\x1b[0m", c);
}

test "clip: no reset appended when colour is off" {
    logo.init(true); // NO_COLOR
    defer logo.init(false);
    var out: [64]u8 = undefined;
    try testing.expectEqualStrings("abc", clip("abcdef", 3, &out));
}

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

test "clipHighlight: washes the selected columns in the accent, keeps the row's own colours" {
    logo.init(false); // colour on
    logo.setAccent(.{ 100, 100, 100 }); // washed at 55% over black → 48;2;55;55;55
    defer logo.setAccent(theme.rgbOf(theme.default_key));
    var out: [256]u8 = undefined;
    // The red fg is preserved; the accent wash brackets visible columns [2,5).
    const r = clipHighlight("\x1b[31mabcdef\x1b[0m", 10, 2, 5, &out);
    try testing.expectEqualStrings("\x1b[31mab\x1b[48;2;55;55;55mcde\x1b[49mf\x1b[0m\x1b[0m", r);
    // A wash reaching the end closes the bg before the final reset.
    try testing.expectEqualStrings("\x1b[48;2;55;55;55mabc\x1b[49m\x1b[0m", clipHighlight("abc", 10, 0, 3, &out));
}

test "visibleSlice: plain visible characters within a column range" {
    var out: [256]u8 = undefined;
    try testing.expectEqualStrings("cd", visibleSlice("\x1b[31mabcdef\x1b[0m", 2, 4, &out));
    try testing.expectEqualStrings("abc", visibleSlice("abc", 0, 99, &out)); // clamps to text end
    try testing.expectEqualStrings("", visibleSlice("abc", 5, 9, &out)); // range past the text
}

test "nextAccentKey: advances presets, wraps, resets from custom" {
    try testing.expectEqualStrings("pink", nextAccentKey("violet")); // first -> second
    try testing.expectEqualStrings("violet", nextAccentKey("grey")); // last wraps to first
    try testing.expectEqualStrings("violet", nextAccentKey("#ff8800")); // custom -> default
    try testing.expectEqualStrings("pink", nextAccentKey("VIOLET")); // case-insensitive
}

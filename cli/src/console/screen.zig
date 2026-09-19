//! Full-screen ("TUI") console renderer for the interactive REPL: pinned logo header,
//! scrollback body, status rule, prompt on the bottom row. SGR mouse tracking drives logo
//! clicks (accent cycle, mirroring the browser logo), wheel scroll, and drag-selection;
//! every `logo.print` is routed here via the output sink, and everything degrades to the
//! plain line editor when the terminal is too small or size detection fails.
const std = @import("std");
const logo = @import("../logo.zig");
const ansi = @import("ansi.zig");
const logoFx = @import("logoFx.zig");
const tty = @import("screen/tty.zig");
const prefs = @import("screen/prefs.zig");
const mouse = @import("screen/mouse.zig");

pub const ttyWrite = tty.ttyWrite;
pub const gotoRow = tty.gotoRow;
pub const gotoClear = tty.gotoClear;
pub const pushChunk = tty.pushChunk;

pub const parseRevealSpeed = prefs.parseRevealSpeed;
pub const speed_min = prefs.speed_min;
pub const speed_max = prefs.speed_max;
pub const speed_default = prefs.speed_default;
pub const nextAccentKey = prefs.nextAccentKey;

pub const Mouse = mouse.Mouse;
pub const parseMouse = mouse.parseMouse;

// One active screen at a time; handlers reach it (for a theme repaint) via `current()`.
pub var g_screen: ?*Screen = null;
pub fn current() ?*Screen {
    return g_screen;
}

pub const max_lines = 5000; // scrollback cap; oldest lines drop past this
pub const wheel_step = 3; // rows per wheel notch
pub const header_pad = 1; // blank rows between the logo header and the output
// `/reveal-speed <speed>`, 0.01 … 1: 1 = instant, smaller = slower (0 would never finish). The
// constants above are the pace at the default 0.5; each scales by (1-speed)/speed, which is 1 there.
pub const reveal_speed_min = 0.01;
pub const reveal_speed_max = 1.0;
pub const reveal_speed_default = 0.5;

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
    // The accent the screen is currently painted in. `logo`'s accent has already moved on by the time
    // onThemeChanged runs, so the outgoing colour is remembered here for the sweep's uncovered side.
    painted_accent: [3]u8 = .{ 124, 58, 237 },
    // How far right the accent reaches on screen, measured when a recolour starts: the seam travels
    // this far, the rule scales its faster progress against it, and the icon's hand turns over it.
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

    /// Enter full-screen mode; on any failure tears down cleanly and errors so the caller falls back to
    /// the plain editor. `self` must have a stable address (its pointer reaches the sink and `g_screen`).
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
    pub const mouseOn = @import("screen/input.zig").mouseOn;
    pub const queryHighlight = @import("screen/select.zig").queryHighlight;
    pub const mousePreference = @import("screen/input.zig").mousePreference;
    pub const revealSpeedPreference = @import("screen/input.zig").revealSpeedPreference;
    pub const revealSpeed = @import("screen/input.zig").revealSpeed;
    pub const setRevealSpeed = @import("screen/input.zig").setRevealSpeed;
    pub const skipRevealOnce = @import("screen/input.zig").skipRevealOnce;
    pub const setSelectionTint = @import("screen/select.zig").setSelectionTint;
    pub const setMouse = @import("screen/input.zig").setMouse;
    pub const freeAll = @import("screen/scrollback.zig").freeAll;
    pub const readByteTimeout = @import("screen/input.zig").readByteTimeout;
    pub const headerRows = @import("screen/model.zig").headerRows;
    pub const bodyRows = @import("screen/model.zig").bodyRows;
    pub const setPromptRows = @import("screen/model.zig").setPromptRows;
    pub const setPromptText = @import("screen/model.zig").setPromptText;
    pub const lineAtRow = @import("screen/model.zig").lineAtRow;
    pub const selectableRow = @import("screen/model.zig").selectableRow;
    pub const maxPromptRows = @import("screen/model.zig").maxPromptRows;
    // The half-open slice [first,end) of `lines` that the body viewport currently shows, honouring
    // the scroll offset. The single source of truth for every paint/extract loop.
    pub const Window = struct { first: usize, end: usize };
    pub const window = @import("screen/model.zig").window;
    pub const contentShown = @import("screen/model.zig").contentShown;
    pub const statusRow = @import("screen/model.zig").statusRow;
    pub const promptRow = @import("screen/model.zig").promptRow;
    pub const promptRows = @import("screen/model.zig").promptRows;
    pub const inHeader = @import("screen/model.zig").inHeader;

    // in-app text selection (visual only)

    pub const SelRange = struct { sr: u16, sc: u16, er: u16, ec: u16 };
    pub const bodyTop = @import("screen/model.zig").bodyTop;
    pub const bodyBottom = @import("screen/model.zig").bodyBottom;
    pub const selActive = @import("screen/select.zig").selActive;
    pub const selStart = @import("screen/select.zig").selStart;
    pub const selDrag = @import("screen/select.zig").selDrag;
    pub const selEnd = @import("screen/select.zig").selEnd;
    pub const hasSelection = @import("screen/select.zig").hasSelection;
    pub const takeSelection = @import("screen/select.zig").takeSelection;
    pub const selNorm = @import("screen/select.zig").selNorm;
    pub const selRowCols = @import("screen/select.zig").selRowCols;
    pub const extractSelection = @import("screen/select.zig").extractSelection;
    pub const querySize = @import("screen/input.zig").querySize;
    pub const tick = @import("screen/input.zig").tick;
    pub const trimBlankEnds = @import("screen/model.zig").trimBlankEnds;
    pub const captureHeader = @import("screen/paint.zig").captureHeader;

    /// Flash the logo one cell smaller and back — the pressed state of a button
    /// for a click on the pinned logo (logoFx owns the animation).
    pub const pressLogo = logoFx.pressLogo;
    pub const onThemeChanged = @import("screen/paint.zig").onThemeChanged;
    pub const sinkTrampoline = @import("screen/scrollback.zig").sinkTrampoline;
    pub const append = @import("screen/scrollback.zig").append;
    pub const wrapNewLines = @import("screen/scrollback.zig").wrapNewLines;
    pub const replaceLine = @import("screen/scrollback.zig").replaceLine;
    pub const removeLine = @import("screen/scrollback.zig").removeLine;
    pub const lineIs = @import("screen/model.zig").lineIs;
    pub const clearScrollback = @import("screen/scrollback.zig").clearScrollback;
    pub const maxScroll = @import("screen/model.zig").maxScroll;
    pub const clampScroll = @import("screen/model.zig").clampScroll;
    pub const scroll = @import("screen/model.zig").scroll;
    pub const fullPaint = @import("screen/paint.zig").fullPaint;
    pub const repaint = @import("screen/paint.zig").repaint;
    pub const paintHeader = @import("screen/paint.zig").paintHeader;
    pub const repaintSelection = @import("screen/paint.zig").repaintSelection;
    pub const selectForTest = @import("screen/select.zig").selectForTest;
    pub const installForTest = @import("screen/input.zig").installForTest;
    pub const uninstallForTest = @import("screen/input.zig").uninstallForTest;
    pub const freeAllForTest = @import("screen/scrollback.zig").freeAllForTest;
    pub const hasHighlight = @import("screen/select.zig").hasHighlight;
    pub const paintPromptSelection = @import("screen/paint.zig").paintPromptSelection;
    pub const paintBody = @import("screen/paint.zig").paintBody;
    pub const paintBodyCut = @import("screen/paint.zig").paintBodyCut;
    pub const drawStatusBar = @import("screen/paint.zig").drawStatusBar;
    pub const drawStatusBarWipe = @import("screen/paint.zig").drawStatusBarWipe;
};

// free helpers (pure, unit-tested)

const testing = std.testing;

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

test {
    _ = @import("screen/model.zig");
    _ = @import("screen/scrollback.zig");
    _ = @import("screen/select.zig");
    _ = @import("screen/paint.zig");
    _ = @import("screen/input.zig");
    _ = tty;
    _ = prefs;
    _ = mouse;
}

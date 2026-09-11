//! Console logo + help text. The logo echoes browser/favicon.svg: a purple rounded
//! panel framing the signature yellow annotation polyline with points. Human
//! output goes to stderr so it never contaminates a piped result; colour is suppressed
//! when NO_COLOR is set, and the `error:`/`note:` prefixes (see err/note below) also need
//! stderr to be a terminal.
const std = @import("std");
const brand = @import("brand.zig");

// Every colour below is the brand triple from themeTokens.json (via brand.zig), turned into
// its SGR escape at compile time — no hex is spelled out twice.
const Ansi = struct {
    const reset = "\x1b[0m";
    const bold = "\x1b[1m";
    const purple = fg(brand.accent); // panel stroke (favicon border)
    const yellow = fg(brand.annotation); // polyline (favicon annotation)
    const frame_bg = bg(brand.panel); // app panel
    const field_bg = bg(brand.panel_inner); // inner image frame
    const grid = fg(brand.panel_grid); // faint point outline / grid dots
    const red = boldFg(brand.error_red); // `error:` prefix
};

fn fg(comptime rgb: [3]u8) []const u8 {
    return std.fmt.comptimePrint("\x1b[38;2;{d};{d};{d}m", .{ rgb[0], rgb[1], rgb[2] });
}
fn bg(comptime rgb: [3]u8) []const u8 {
    return std.fmt.comptimePrint("\x1b[48;2;{d};{d};{d}m", .{ rgb[0], rgb[1], rgb[2] });
}
fn boldFg(comptime rgb: [3]u8) []const u8 {
    return std.fmt.comptimePrint("\x1b[1;38;2;{d};{d};{d}m", .{ rgb[0], rgb[1], rgb[2] });
}

var use_color: std.atomic.Value(bool) = .init(true);

// Atomic: a worker thread prints through here (scrape fans its fetches out) and init is the
// sole writer. Severity colour is gated separately — only a real terminal gets it, so piped
// output and every sink-capturing test keep `error: `/`note: ` plain for grep and CI logs.
var severity_color: std.atomic.Value(bool) = .init(false);

// Whether stderr is a terminal at all (init's `tty`), for output that redraws a row in place.
var human_tty: std.atomic.Value(bool) = .init(false);

// The brand accent (logo panel outline, prompt, echoed commands). Defaults to the canonical brand
// violet; `/theme` swaps it — main loop only, never under a fan-out. `accent_slice` caches its SGR.
var accent_rgb: [3]u8 = brand.accent;
var accent_buf: [20]u8 = undefined;
var accent_slice: []const u8 = "";

fn refreshAccent() void {
    accent_slice = if (use_color.load(.monotonic))
        std.fmt.bufPrint(&accent_buf, "\x1b[38;2;{d};{d};{d}m", .{ accent_rgb[0], accent_rgb[1], accent_rgb[2] }) catch ""
    else
        "";
}

/// Enable colour unless NO_COLOR is set (the caller checks the environment); `tty` is
/// whether the human channel (stderr) is a terminal, which additionally gates the
/// severity prefixes.
pub fn init(no_color: bool, tty: bool) void {
    use_color.store(!no_color, .monotonic);
    severity_color.store(!no_color and tty, .monotonic);
    human_tty.store(tty, .monotonic);
    refreshAccent();
}

/// True when human output goes straight to a terminal — stderr is a tty and no sink is
/// installed — the one case a row can be rewritten in place with a carriage return.
pub fn liveTty() bool {
    return human_tty.load(.monotonic) and sink_fn == null;
}

/// Repaint the brand accent (logo outline, prompt, command echo) to an RGB triple.
pub fn setAccent(rgb: [3]u8) void {
    accent_rgb = rgb;
    refreshAccent();
}

// When set (by the full-screen console), accentSeq() returns a one-byte SENTINEL (0x01) instead
// of the literal escape, so accent-coloured output stored in the scrollback is re-tinted to the
// *current* accent every repaint (screen.clip expands 0x01 → accentReal()). This is what lets a
// theme change recolour already-printed help/echoes. Direct-to-terminal writers (the prompt) use
// accentReal() so they never emit the raw sentinel.
var accent_sentinel_on: std.atomic.Value(bool) = .init(false);
pub const accent_sentinel = "\x01";
pub fn setAccentSentinel(on: bool) void {
    accent_sentinel_on.store(on, .monotonic);
}

/// SGR escape for the current accent, and the reset; both "" when colour is off. Used by
/// the line editor to colour the prompt and the typed command. In sentinel mode this returns
/// the 0x01 placeholder instead (see setAccentSentinel).
pub fn accentSeq() []const u8 {
    return if (accent_sentinel_on.load(.monotonic)) accent_sentinel else accent_slice;
}

/// The real accent SGR escape, never the sentinel — for direct terminal writes.
pub fn accentReal() []const u8 {
    return accent_slice;
}

/// The current accent as a raw RGB triple — used by the full-screen console to tint the
/// text-selection highlight with a translucent wash of the live theme colour.
pub fn accentRgb() [3]u8 {
    return accent_rgb;
}
pub fn resetSeq() []const u8 {
    return c(Ansi.reset);
}
pub fn colorEnabled() bool {
    return use_color.load(.monotonic);
}

fn c(comptime code: []const u8) []const u8 {
    return if (use_color.load(.monotonic)) code else "";
}

// Optional output sink. When set (by the full-screen console in screen.zig), every `print`
// is routed here instead of straight to stderr, so human output can be captured into the
// scrollback buffer and redrawn inside the pinned-header viewport. It's also reused to
// capture `banner()` into the fixed header. Unset (the default) = plain stderr, so one-shot
// mode, piped console input and CI are completely unaffected.
var sink_fn: ?*const fn (*anyopaque, []const u8) void = null;
var sink_ctx: *anyopaque = undefined;

/// Route subsequent `print` output to `f` instead of stderr.
pub fn setSink(f: *const fn (*anyopaque, []const u8) void, ctx: *anyopaque) void {
    sink_fn = f;
    sink_ctx = ctx;
}

/// Restore the default stderr destination.
pub fn clearSink() void {
    sink_fn = null;
}

// A one-shot hook fired just BEFORE the next print, then disarmed. The line editor arms it
// around a slow call (reading an image off the clipboard) so the prompt row is erased at the
// instant a message actually arrives — erasing it up front left the input blank for as long
// as the read took, which reads as a blink.
var pre_print_fn: ?*const fn (*anyopaque) void = null;
var pre_print_ctx: *anyopaque = undefined;

/// Arm the one-shot pre-print hook (replacing any armed one).
pub fn armPrePrint(f: *const fn (*anyopaque) void, ctx: *anyopaque) void {
    pre_print_fn = f;
    pre_print_ctx = ctx;
}

/// Disarm it — always paired with armPrePrint, since a hook that never fires must not
/// outlive the call it was armed for.
pub fn disarmPrePrint() void {
    pre_print_fn = null;
}

/// Print to the CLI's human channel — stderr by default, or the active sink (the full-screen
/// scrollback) when one is installed. On a formatting overflow it falls back to stderr.
pub fn print(comptime fmt: []const u8, args: anytype) void {
    if (pre_print_fn) |f| {
        const ctx = pre_print_ctx;
        pre_print_fn = null; // disarm FIRST: the hook itself may print
        f(ctx);
    }
    if (sink_fn) |f| {
        var buf: [8192]u8 = undefined;
        if (std.fmt.bufPrint(&buf, fmt, args)) |s| {
            f(sink_ctx, s);
            return;
        } else |_| {} // too long for one chunk — fall through to stderr
    }
    std.debug.print(fmt, args);
}

// The CLI's whole severity vocabulary: `error: ` (the command did not do what was asked)
// and `note: ` (it went ahead, with something worth saying). Word prefixes, never emoji —
// they are the Unix convention that grep, CI logs and the mcp/bot adapters parse. Go
// through err()/note() rather than writing the literal, so the wording and the colouring
// have exactly one definition. A listing/query answering "there are none" is a truthful
// answer, not a refusal: it stays plain (`/connections` with no servers), while a command
// that tried to act and could not is an `error:` (`/disconnect` with no servers).

/// The `error: ` prefix — bold red on a colour terminal, plain elsewhere.
pub fn errPrefix() []const u8 {
    return if (severity_color.load(.monotonic)) Ansi.red ++ "error: " ++ Ansi.reset else "error: ";
}

/// The `note: ` prefix — the live THEME accent on a colour terminal, plain elsewhere. It uses
/// accentSeq() (not accentReal()), so in the full-screen console a note already in the
/// scrollback is re-tinted when the theme changes, like every other accent-coloured line.
/// `error:` stays red: severity that means "this did not happen" should not move with the theme.
pub fn notePrefix() []const u8 {
    if (!severity_color.load(.monotonic)) return "note: ";
    // Bold FIRST, then the accent: `error:` is bold red, so the two severities carry the same
    // weight and differ only in hue. In sentinel mode the accent is one byte the screen expands
    // on every repaint, and the bold in front of it survives that expansion untouched.
    const accent = accentSeq();
    const reset = c(Ansi.reset);
    const parts = [_][]const u8{ Ansi.bold, accent, "note: ", reset };
    var n: usize = 0;
    for (parts) |part| n += part.len;
    if (n > note_prefix_buf.len) return "note: ";
    n = 0;
    for (parts) |part| {
        @memcpy(note_prefix_buf[n..][0..part.len], part);
        n += part.len;
    }
    return note_prefix_buf[0..n];
}

/// Scratch for notePrefix (the accent is not comptime). Thread-local: a worker printing a
/// fetch failure must not share it with the console.
threadlocal var note_prefix_buf: [64]u8 = undefined;

/// Print an `error: ` line (the message must supply its own trailing newline).
pub fn err(comptime fmt: []const u8, args: anytype) void {
    print("{s}", .{errPrefix()});
    print(fmt, args);
}

/// Print a `note: ` line (the message must supply its own trailing newline).
pub fn note(comptime fmt: []const u8, args: anytype) void {
    print("{s}", .{notePrefix()});
    print(fmt, args);
}

// A larger text rendering of browser/favicon.svg, laid out to read square in a
// terminal (cells are ~2:1 tall, so the panel spans about twice as many columns as
// rows). It reproduces the icon's pieces: a purple rounded panel (the curved corners
// echo the SVG's rx="13"), the dark app panel (frame_bg) forming a margin around the
// lighter inner image frame (field_bg), and the signature yellow annotation polyline
// with a round point (●) at each vertex.
//
// FRAME_W/FRAME_H is the lighter inner frame; the polyline is rasterised at runtime
// from the favicon's S-mark vertices, mapped into the frame.
// Mh/Mv is the dark app-panel margin around it; the rounded purple border is drawn
// outside that.
const FRAME_W = 14; // lighter inner frame width, in cells
const FRAME_H = 6; // lighter inner frame height, in rows
const Mh = 1; // horizontal dark app-panel margin (cells)
const Mv = 0; // vertical dark app-panel margin (rows); curve rows supply the dark cap
const PANEL_W = FRAME_W + Mh * 2; // inner width between the side borders
const BODY_H = FRAME_H + Mv * 2; // inner height between the top/bottom borders

const Pt = struct { col: usize, row: usize };
// Favicon vertices mapped into the FRAME_W×FRAME_H cell grid. Cells are ~2:1 tall, so the
// S is snapped to the grid rather than scaled from the SVG: the bars land ON a row (drawn
// with ─) and the joins step one row at a time, which is what keeps it legible at 14×6.
const verts = [_]Pt{
    .{ .col = 11, .row = 0 }, // top-right end
    .{ .col = 3, .row = 0 }, // top bar, running left
    .{ .col = 1, .row = 1 }, // down the left side
    .{ .col = 3, .row = 2 }, // back onto the middle row
    .{ .col = 10, .row = 2 }, // middle bar, running right
    .{ .col = 12, .row = 3 }, // down the right side
    .{ .col = 11, .row = 5 }, // …to the bottom row
    .{ .col = 2, .row = 5 }, // bottom bar, running left
};

// The SMALL mark: the same S snapped into a 10×5 grid, for the pressed-logo frame (the whole
// icon shrinks — panel, dark margin and artwork together — so the click reads as a button
// going down, not as a border losing a ring).
const FRAME_W_S = 10;
const FRAME_H_S = 5;
const verts_small = [_]Pt{
    .{ .col = 8, .row = 0 }, // top-right end
    .{ .col = 2, .row = 0 }, // top bar, running left
    .{ .col = 1, .row = 1 }, // down the left side
    .{ .col = 2, .row = 2 }, // back onto the middle row
    .{ .col = 7, .row = 2 }, // middle bar, running right
    .{ .col = 8, .row = 3 }, // down the right side
    .{ .col = 7, .row = 4 }, // …to the bottom row
    .{ .col = 1, .row = 4 }, // bottom bar, running left — one cell wider than the middle one,
}; // so the three bars stagger and still read as an S at half size

// Glyph codes laid into the rasterised frame.
const G_SPACE = 0;
const G_UP = 1; // ╱ (segment rising left→right)
const G_DOWN = 2; // ╲ (segment falling left→right)
const G_MARK = 3; // ● (polyline vertex)
const G_FLAT = 4; // ─ (segment level across a row — the S's bars)

fn glyph(code: u8) []const u8 {
    return switch (code) {
        G_UP => "╱",
        G_DOWN => "╲",
        G_FLAT => "─",
        G_MARK => "●",
        else => " ",
    };
}

// Rasterise a polyline into a W×H grid: straight strokes between vertices (slope picks ╱ or
// ╲), with a ● dropped on each vertex. Both sizes of the mark go through here — the small one
// is its own hand-snapped vertex set, not a scaled copy, because rounding a 14×6 S into 10×5
// collapses its bars onto their joins.
fn rasterise(comptime W: usize, comptime H: usize, comptime vs: []const Pt) [H][W]u8 {
    var g = std.mem.zeroes([H][W]u8);
    for (0..vs.len - 1) |s| {
        const a = vs[s];
        const z = vs[s + 1];
        // The glyph must follow the segment's real slope, which needs BOTH deltas: the S
        // runs right→left across its bars, so a down-LEFT join is ╱, not ╲. A level run
        // gets its own glyph.
        const down_right = (z.row > a.row) == (z.col > a.col);
        const stroke: u8 = if (z.row == a.row) G_FLAT else if (down_right) G_DOWN else G_UP;
        const dc = @as(i32, @intCast(z.col)) - @as(i32, @intCast(a.col));
        const dr = @as(i32, @intCast(z.row)) - @as(i32, @intCast(a.row));
        const steps = @max(@abs(dc), @abs(dr));
        var i: i32 = 1;
        while (i < steps) : (i += 1) {
            const t = @as(f64, @floatFromInt(i)) / @as(f64, @floatFromInt(steps));
            const cf = @as(f64, @floatFromInt(a.col)) + @as(f64, @floatFromInt(dc)) * t;
            const rf = @as(f64, @floatFromInt(a.row)) + @as(f64, @floatFromInt(dr)) * t;
            const cc: usize = @intFromFloat(@round(cf));
            const rr: usize = @intFromFloat(@round(rf));
            if (g[rr][cc] == G_SPACE) g[rr][cc] = stroke;
        }
    }
    for (vs) |v| g[v.row][v.col] = G_MARK;
    return g;
}

fn spaces(n: usize) void {
    var i: usize = 0;
    while (i < n) : (i += 1) print(" ", .{});
}

fn rule(comptime g: []const u8, width: usize) void {
    var i: usize = 0;
    while (i < width) : (i += 1) print(g, .{});
}

pub fn banner() void {
    emitBanner(false);
}

/// The logo at its pressed size: the whole icon — rounded panel, dark margin and the S mark
/// inside it — redrawn about two cells smaller on each side (18×10 → 14×7 cells) around the
/// smaller mark. The full-screen console flashes this frame for a moment when the logo is
/// clicked, which reads as a button going down. The wordmark is NOT part of it: the press moves
/// the icon only, and the screen paints this over the icon's columns alone.
pub fn bannerCompact() void {
    emitBanner(true);
}

fn emitBanner(comptime compact: bool) void {
    const p = accent_slice; // brand accent (violet by default) — themeable via /theme
    const y = c(Ansi.yellow);
    const b = c(Ansi.bold);
    const r = c(Ansi.reset);
    const fbg = c(Ansi.frame_bg);
    const ibg = c(Ansi.field_bg);

    // Both sizes are the same drawing at two scales; only the frame constants change. The
    // pressed one is indented further so the smaller icon stays centred on the space the full
    // one occupies, and it drops the curved caps (there is no room for them at 7 rows).
    const fw = if (compact) FRAME_W_S else FRAME_W;
    const fh = if (compact) FRAME_H_S else FRAME_H;
    const grid = rasterise(fw, fh, if (compact) &verts_small else &verts);
    const panel_w = fw + Mh * 2;
    const body_h = fh + Mv * 2;
    const indent: usize = if (compact) 4 else 2;

    print("\n", .{});
    // Rounded top: an inset ╭──╮ with ╱ ╲ curving out to the full-width sides — a text
    // approximation of the SVG's rounded corners (rx="13").
    spaces(indent);
    print("{s}{s}╭", .{ p, if (compact) "" else " " });
    rule("─", if (compact) panel_w else panel_w - 2);
    print("╮{s}\n", .{r});
    if (!compact) {
        spaces(indent);
        print("{s}╱{s}", .{ p, fbg }); // dark app-panel fills the curve, no black gap
        spaces(panel_w);
        print("{s}{s}╲{s}\n", .{ r, p, r });
    }

    // Inner rows: side border, dark margin, lighter image frame, dark margin, side
    // border. The wordmark sits to the right of the panel, vertically centred.
    const label_row = body_h / 2;
    var row_idx: usize = 0;
    while (row_idx < body_h) : (row_idx += 1) {
        spaces(indent);
        print("{s}│{s}{s}", .{ p, r, fbg }); // left border, then dark app panel
        spaces(Mh); // left dark margin
        if (row_idx >= Mv and row_idx < Mv + fh) {
            const fr = row_idx - Mv;
            print("{s}{s}", .{ ibg, y }); // lighter image frame, yellow annotation
            for (grid[fr]) |code| print("{s}", .{glyph(code)});
            print("{s}", .{fbg}); // back to dark for the right margin
        } else {
            spaces(fw); // dark margin row (top / bottom of the inner frame)
        }
        spaces(Mh); // right dark margin
        print("{s}{s}│{s}", .{ r, p, r }); // right border on default bg
        if (!compact and row_idx == label_row) print("   {s}S T E N C I L{s}", .{ b, r });
        print("\n", .{});
    }

    if (!compact) {
        spaces(indent);
        print("{s}╲{s}", .{ p, fbg }); // dark app-panel fills the curve, no black gap
        spaces(panel_w);
        print("{s}{s}╱{s}\n", .{ r, p, r });
    }
    spaces(indent);
    print("{s}{s}╰", .{ p, if (compact) "" else " " });
    rule("─", if (compact) panel_w else panel_w - 2);
    print("╯{s}\n\n", .{r});
}

pub fn usage() void {
    emitMarked(help_text, c(Ansi.bold), c(Ansi.reset));
}

// The `--help` prose lives in help.txt (embedded), not in this file: one list of flags for
// people, cross-checked against args.zig's flag table by tests/help_flags_test.zig. `{b}` /
// `{r}` are the only markers — bold on, bold off — and both are "" when colour is off.
const help_text = @embedFile("help.txt");

/// Print `text`, expanding the `{b}`/`{r}` markers; any other `{` is literal.
fn emitMarked(text: []const u8, bold: []const u8, reset: []const u8) void {
    var rest = text;
    while (std.mem.indexOfScalar(u8, rest, '{')) |at| {
        if (at != 0) print("{s}", .{rest[0..at]});
        const tail = rest[at..];
        if (std.mem.startsWith(u8, tail, "{b}")) {
            print("{s}", .{bold});
            rest = tail[3..];
        } else if (std.mem.startsWith(u8, tail, "{r}")) {
            print("{s}", .{reset});
            rest = tail[3..];
        } else {
            print("{{", .{});
            rest = tail[1..];
        }
    }
    if (rest.len != 0) print("{s}", .{rest});
}

const testing = std.testing;

// Collects `print` output through the same sink seam the full-screen console installs.
const Cap = struct {
    buf: std.ArrayList(u8) = .empty,
    fn sink(ctx: *anyopaque, bytes: []const u8) void {
        const self: *Cap = @ptrCast(@alignCast(ctx));
        self.buf.appendSlice(testing.allocator, bytes) catch {};
    }
};

test "err/note are byte-for-byte plain when stderr is not a terminal" {
    var cap = Cap{};
    defer cap.buf.deinit(testing.allocator);
    setSink(Cap.sink, &cap);
    defer clearSink();
    defer init(false, false); // module defaults, for the tests that follow

    init(false, false); // colour on, but the human channel is redirected
    err("cannot read '{s}': {s}\n", .{ "a.png", "FileNotFound" });
    note("skipped save — no working image to save\n", .{});
    try testing.expectEqualStrings(
        "error: cannot read 'a.png': FileNotFound\nnote: skipped save — no working image to save\n",
        cap.buf.items,
    );
}

test "err/note colour only the prefix on a terminal, and NO_COLOR turns it off" {
    var cap = Cap{};
    defer cap.buf.deinit(testing.allocator);
    setSink(Cap.sink, &cap);
    defer clearSink();
    defer init(false, false);

    init(false, true); // colour on + a terminal
    err("boom\n", .{});
    try testing.expectEqualStrings("\x1b[1;38;2;239;68;68merror: \x1b[0mboom\n", cap.buf.items);

    // `note:` wears the LIVE theme accent, not a fixed amber — so it follows /theme.
    cap.buf.clearRetainingCapacity();
    setAccent(.{ 10, 20, 30 });
    note("hm\n", .{});
    try testing.expectEqualStrings("\x1b[1m\x1b[38;2;10;20;30mnote: \x1b[0mhm\n", cap.buf.items);

    cap.buf.clearRetainingCapacity();
    setAccent(.{ 200, 100, 50 });
    note("hm\n", .{});
    try testing.expectEqualStrings("\x1b[1m\x1b[38;2;200;100;50mnote: \x1b[0mhm\n", cap.buf.items);

    // `error:` does NOT move with the theme — red is the one severity that stays put.
    cap.buf.clearRetainingCapacity();
    err("boom\n", .{});
    try testing.expectEqualStrings("\x1b[1;38;2;239;68;68merror: \x1b[0mboom\n", cap.buf.items);

    cap.buf.clearRetainingCapacity();
    init(true, true); // NO_COLOR wins over the terminal
    err("boom\n", .{});
    try testing.expectEqualStrings("error: boom\n", cap.buf.items);
}

// Only the PRESENTATION layer talks to a terminal. Everything below it — pipeline, net,
// project, scrape, serverClient, llm/, the codecs — reports through report.zig, so the same
// code runs headlessly behind another sink. Both rules are linted over the sources: the
// severity prefixes have one definition (err()/note(), never a literal), and no file below
// the line reaches for logo or an ANSI escape. The lint WALKS src/ rather than reading an
// embedded list, so a new file below the line is caught the day it lands.

/// Files that may paint a terminal: this module, the sink in front of it, the entry points,
/// and the two interactive surfaces (see presentation_dirs for their packages).
const presentation = [_][]const u8{
    "logo.zig",        "report.zig",     "main.zig",     "bench.zig",
    "args.zig",        "brand.zig",      "messages.zig", "theme.zig",
    "project_cli.zig", "line_edit.zig",  "console.zig",
};
const presentation_dirs = [_][]const u8{ "console/", "line_edit/", "params/" };

fn isPresentation(rel: []const u8) bool {
    for (presentation) |p| if (std.mem.eql(u8, rel, p)) return true;
    for (presentation_dirs) |d| if (std.mem.startsWith(u8, rel, d)) return true;
    return false;
}

/// The shipped half of a source file: everything before the first column-0 `test`, so an
/// assertion QUOTING a prefix or an escape never counts as a call site.
fn productionPart(src: []const u8) []const u8 {
    var i: usize = 0;
    while (std.mem.indexOfPos(u8, src, i, "\ntest ")) |at| {
        if (src[at + 6] == '"' or src[at + 6] == '{') return src[0 .. at + 1];
        i = at + 1;
    }
    return src;
}

test "layering: severity has one definition, and only the presentation layer prints" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    // cwd is cli/ under `zig build test`; tolerate a run from the repo root.
    var src_dir = std.Io.Dir.cwd().openDir(io, "src", .{ .iterate = true }) catch
        try std.Io.Dir.cwd().openDir(io, "cli/src", .{ .iterate = true });
    defer src_dir.close(io);

    const literals = [_][]const u8{ "\"error: ", "\"note: ", "\"warning: " };
    const prints = [_][]const u8{ "logo.print(", "logo.err(", "logo.note(", "logo.banner(" };
    const escapes = [_][]const u8{ "\\x1b", "\\x1B", "\\u{1b}", "\\033", "\x1b" };

    var seen: usize = 0;
    var failures: usize = 0;
    var walker = try src_dir.walk(a);
    defer walker.deinit();
    while (try walker.next(io)) |e| {
        if (e.kind != .file or !std.mem.endsWith(u8, e.basename, ".zig")) continue;
        const rel = try a.dupe(u8, e.path);
        std.mem.replaceScalar(u8, rel, '\\', '/'); // walker paths are host-separated
        const prod = productionPart(try e.dir.readFileAlloc(io, e.basename, a, .limited(4 << 20)));
        seen += 1;

        // Every layer: the `error: `/`note: ` wording and colouring live in err()/note().
        if (!std.mem.eql(u8, rel, "logo.zig")) {
            for (literals) |lit| if (std.mem.indexOf(u8, prod, lit) != null) {
                std.debug.print("LITERAL PREFIX: {s} spells {s} itself — call err()/note()\n", .{ rel, lit });
                failures += 1;
            };
        }
        if (isPresentation(rel)) continue;

        // Below the line: no terminal at all — report.zig is the only way out.
        for (prints) |call| if (std.mem.indexOf(u8, prod, call) != null) {
            std.debug.print("LAYER BREAK: {s} calls {s} — go through report.zig\n", .{ rel, call });
            failures += 1;
        };
        for (escapes) |esc| if (std.mem.indexOf(u8, prod, esc) != null) {
            std.debug.print("LAYER BREAK: {s} writes an ANSI escape — styling is the console's\n", .{rel});
            failures += 1;
        };
    }
    try testing.expect(seen >= 30); // the tree really was walked
    try testing.expectEqual(@as(usize, 0), failures);
}

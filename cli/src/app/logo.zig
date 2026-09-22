//! Console logo + help text. The logo echoes browser/favicon.svg: a purple rounded
//! panel framing the signature yellow annotation polyline with points. Human
//! output goes to stderr so it never contaminates a piped result; colour is suppressed
//! when NO_COLOR is set, and the `error:`/`note:` prefixes (see err/note below) also need
//! stderr to be a terminal.
const std = @import("std");
const brand = @import("brand.zig");
const palette = @import("logo/palette.zig");
const mark = @import("logo/mark.zig");
const help = @import("logo/help.zig");

const Ansi = palette.Ansi;
const fg = palette.fg;

pub const banner = mark.banner;
pub const bannerCompact = mark.bannerCompact;
pub const usage = help.usage;

var use_color: std.atomic.Value(bool) = .init(true);

// Atomic: a worker thread prints through here (scrape fans its fetches out) and init is the sole
// writer. Severity colour is gated separately, so piped output and test sinks stay plain for grep.
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

/// Enable colour unless NO_COLOR is set (the caller checks the environment); `tty` is whether stderr
/// is a terminal, which additionally gates the severity prefixes.
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

// When set (by the full-screen console) accentSeq() returns a one-byte SENTINEL (0x01) instead of the
// escape, so stored output re-tints on every repaint (screen.clip expands 0x01 → accentReal()).
var accent_sentinel_on: std.atomic.Value(bool) = .init(false);
pub const accent_sentinel = "\x01";
pub fn setAccentSentinel(on: bool) void {
    accent_sentinel_on.store(on, .monotonic);
}

/// SGR escape for the current accent, and the reset; both "" when colour is off. In sentinel mode this
/// returns the 0x01 placeholder instead (see setAccentSentinel).
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

/// An SGR escape, or "" when colour is off. `mark`/`help` paint through it too.
pub fn colorSeq(comptime code: []const u8) []const u8 {
    return if (use_color.load(.monotonic)) code else "";
}
const c = colorSeq;

// Optional output sink. When set (by screen.zig) every `print` is routed here instead of stderr, so
// output can be captured into the scrollback. Unset = plain stderr, so one-shot mode is unaffected.
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

// A one-shot hook fired just BEFORE the next print, then disarmed. The line editor arms it around a
// slow clipboard read, so the prompt row is erased when a message arrives rather than up front.
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

// The CLI's whole severity vocabulary: `error: ` and `note: `, word prefixes rather than emoji — the
// convention grep, CI logs and the mcp/bot adapters parse. Go through err()/note(), never a literal.

/// The `error: ` prefix — bold red on a colour terminal, plain elsewhere.
pub fn errPrefix() []const u8 {
    return if (severity_color.load(.monotonic)) Ansi.red ++ "error: " ++ Ansi.reset else "error: ";
}

/// The `note: ` prefix — the live THEME accent on a colour terminal, via accentSeq() so a note in the
/// scrollback re-tints. `error:` stays red: "this did not happen" should not move with the theme.
pub fn notePrefix() []const u8 {
    if (!severity_color.load(.monotonic)) return "note: ";
    // Bold FIRST, then the accent: `error:` is bold red, so the two severities carry the same weight and
    // differ only in hue. In sentinel mode the bold in front survives the accent's expansion.
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

// Only the PRESENTATION layer talks to a terminal; everything below it reports through report.zig.
// The lint WALKS src/ rather than an embedded list, so a new file below the line is caught at once.

/// Files that may paint a terminal: this module, the sink in front of it, the entry points,
/// and the two interactive surfaces (see presentation_dirs for their packages).
const presentation = [_][]const u8{
    "main.zig", "args.zig", "line_edit.zig", "console.zig", "project/cli.zig",
};
const presentation_dirs = [_][]const u8{ "app/", "bench/", "console/", "line_edit/", "params/" };

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
    const prints = [_][]const u8{ "logo.print(", "logo.err(", "logo.note(", "logo.banner(", "std.debug.print(" };
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
        if (!std.mem.eql(u8, rel, "app/logo.zig")) {
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

test {
    _ = palette;
    _ = mark;
    _ = help;
}

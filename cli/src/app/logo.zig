//! Console logo + help text. The logo echoes common/icons/favicon.svg: a purple rounded
//! panel framing the signature yellow annotation polyline with points. Human
//! output goes to stderr so it never contaminates a piped result; colour is suppressed
//! when NO_COLOR is set, and the `error:`/`note:` prefixes (see err/note below) also need
//! stderr to be a terminal.
const std = @import("std");
const brand = @import("brand.zig");
const palette = @import("logo/palette.zig");
const mark = @import("logo/mark.zig");
const help = @import("logo/help.zig");
const severity = @import("logo/severity.zig");
const deferred = @import("logo/deferred.zig");

const Ansi = palette.Ansi;

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
    flushDeferred();
    deferred.own();
    sink_fn = f;
    sink_ctx = ctx;
}

/// Restore the default stderr destination.
pub fn clearSink() void {
    flushDeferred();
    sink_fn = null;
    if (pre_print_fn == null) release();
}

// A one-shot hook fired just BEFORE the next print, then disarmed. The line editor arms it around a
// slow clipboard read, so the prompt row is erased when a message arrives rather than up front.
var pre_print_fn: ?*const fn (*anyopaque) void = null;
var pre_print_ctx: *anyopaque = undefined;

/// Arm the one-shot pre-print hook (replacing any armed one).
pub fn armPrePrint(f: *const fn (*anyopaque) void, ctx: *anyopaque) void {
    flushDeferred();
    deferred.own();
    pre_print_fn = f;
    pre_print_ctx = ctx;
}

/// Disarm it — always paired with armPrePrint, since a hook that never fires must not
/// outlive the call it was armed for.
pub fn disarmPrePrint() void {
    flushDeferred();
    pre_print_fn = null;
    if (sink_fn == null) release();
}

/// Print to the CLI's human channel — stderr, or the sink (the full-screen scrollback) when one is
/// installed. Off the thread that installed it or armed the hook, the line waits for that thread.
pub fn print(comptime fmt: []const u8, args: anytype) void {
    if (deferred.mine()) {
        flushDeferred();
        if (sink_fn != null) return formatted(fmt, args, emitOwned);
        firePrePrint();
        return std.debug.print(fmt, args);
    }
    if (deferred.owned()) return formatted(fmt, args, emitElsewhere);
    std.debug.print(fmt, args);
}

/// Emit what other threads printed while this one owned the channel, in the order they said it; a
/// no-op on any other thread. The owner calls it on its wait beat and when a worker it joined ends.
pub fn flushDeferred() void {
    var said = deferred.take(false);
    defer said.deinit(deferred.gpa);
    if (said.items.len != 0) emitOwned(said.items);
}

fn release() void {
    var said = deferred.take(true);
    defer said.deinit(deferred.gpa);
    if (said.items.len != 0) std.debug.print("{s}", .{said.items});
}

fn firePrePrint() void {
    const f = pre_print_fn orelse return;
    pre_print_fn = null; // disarm FIRST: the hook itself may print
    f(pre_print_ctx);
}

fn emitOwned(bytes: []const u8) void {
    firePrePrint();
    if (sink_fn) |f| return f(sink_ctx, bytes);
    std.debug.print("{s}", .{bytes});
}

fn emitElsewhere(bytes: []const u8) void {
    if (!deferred.push(bytes)) std.debug.print("{s}", .{bytes});
}

// `fmt` rendered whole, then handed on: one chunk on the stack, else the heap (a long line still
// lands whole), else as much as fits.
fn formatted(comptime fmt: []const u8, args: anytype, emit: *const fn ([]const u8) void) void {
    var buf: [8192]u8 = undefined;
    if (std.fmt.bufPrint(&buf, fmt, args)) |s| return emit(s) else |_| {}
    const big = std.fmt.allocPrint(std.heap.page_allocator, fmt, args) catch {
        var w = std.Io.Writer.fixed(&buf);
        w.print(fmt, args) catch {};
        return emit(w.buffered());
    };
    defer std.heap.page_allocator.free(big);
    emit(big);
}

// The CLI's whole severity vocabulary, `error: ` and `note: `: logo/severity.zig.
pub const errPrefix = severity.errPrefix;
pub const notePrefix = severity.notePrefix;
pub const err = severity.err;
pub const note = severity.note;

/// Whether the severity prefixes are coloured: colour on and stderr a terminal (see init).
pub fn severityColor() bool {
    return severity_color.load(.monotonic);
}

test {
    _ = @import("lint.zig");
    _ = severity;
    _ = deferred;
    _ = palette;
    _ = mark;
    _ = @import("logo/eggArt.zig");
    _ = help;
    _ = @import("skin.zig");
}

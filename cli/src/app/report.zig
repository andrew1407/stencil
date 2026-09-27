//! The report sink: how code BELOW the console layer says something to the user. pipeline,
//! net, project, scrape, serverClient and llm/ call report.print/err/note and never reach for
//! logo.zig's ANSI, banner or usage surface — so they can run headlessly (a server, a test, a
//! library embedding) by installing a different Writer. The default forwards to logo, so the
//! wording and the colouring keep exactly one definition. logo.zig's layer lint enforces this.
const std = @import("std");
const logo = @import("logo.zig");

/// `error: ` (the command did not do what was asked), `note: ` (it went ahead, with something
/// worth saying), or plain — the CLI's whole severity vocabulary, see logo.zig.
pub const Severity = enum { plain, err, note };

/// Where the lower layers' lines go. `emitFn` receives the formatted message WITHOUT a
/// severity prefix; the writer owns how (or whether) a severity is rendered.
pub const Writer = struct {
    ctx: *anyopaque,
    emitFn: *const fn (*anyopaque, Severity, []const u8) void,
};

var installed: ?Writer = null;

/// Route the lower layers' output to `w` instead of the terminal.
pub fn install(w: Writer) void {
    installed = w;
}

/// Restore the default (logo) destination.
pub fn uninstall() void {
    installed = null;
}

/// A plain line (the message supplies its own trailing newline).
pub fn print(comptime fmt: []const u8, args: anytype) void {
    emit(.plain, fmt, args);
}

/// An `error: ` line — the operation did not happen.
pub fn err(comptime fmt: []const u8, args: anytype) void {
    emit(.err, fmt, args);
}

/// A `note: ` line — it went ahead, with something worth saying.
pub fn note(comptime fmt: []const u8, args: anytype) void {
    emit(.note, fmt, args);
}

/// Emit what a worker printed while this thread owned the terminal (logo.flushDeferred): a
/// thread waiting on a worker calls it as it waits and once the worker is done.
pub fn flushDeferred() void {
    logo.flushDeferred();
}

/// The `error: ` prefix, for a caller that composes the whole line itself (scrape's Deps
/// sink, which hands finished text to one emit function).
pub fn errPrefix() []const u8 {
    return logo.errPrefix();
}

fn emit(sev: Severity, comptime fmt: []const u8, args: anytype) void {
    if (installed) |w| {
        var buf: [8192]u8 = undefined;
        if (std.fmt.bufPrint(&buf, fmt, args)) |s| return w.emitFn(w.ctx, sev, s) else |_| {}
        // Longer than one chunk: the heap, so the line arrives whole; without memory, what fits.
        const big = std.fmt.allocPrint(std.heap.page_allocator, fmt, args) catch {
            var fw = std.Io.Writer.fixed(&buf);
            fw.print(fmt, args) catch {};
            return w.emitFn(w.ctx, sev, fw.buffered());
        };
        defer std.heap.page_allocator.free(big);
        w.emitFn(w.ctx, sev, big);
        return;
    }
    switch (sev) {
        .plain => logo.print(fmt, args),
        .err => logo.err(fmt, args),
        .note => logo.note(fmt, args),
    }
}

test "an installed writer takes the lower layers' lines, with the severity and no prefix" {
    const Cap = struct {
        var sev: Severity = .plain;
        var buf: [64]u8 = undefined;
        var len: usize = 0;
        fn take(_: *anyopaque, s: Severity, text: []const u8) void {
            sev = s;
            len = @min(text.len, buf.len);
            @memcpy(buf[0..len], text[0..len]);
        }
    };
    var unused: u8 = 0;
    install(.{ .ctx = @ptrCast(&unused), .emitFn = Cap.take });
    defer uninstall();
    err("no source for '{s}'\n", .{"x.png"});
    try std.testing.expectEqual(Severity.err, Cap.sev);
    try std.testing.expectEqualStrings("no source for 'x.png'\n", Cap.buf[0..Cap.len]);
}

test "a line past 8 KB reaches an installed writer whole, never dropped" {
    const Cap = struct {
        var len: usize = 0;
        fn take(_: *anyopaque, _: Severity, text: []const u8) void {
            len = text.len;
        }
    };
    var unused: u8 = 0;
    install(.{ .ctx = @ptrCast(&unused), .emitFn = Cap.take });
    defer uninstall();
    const long = "y" ** 12000;
    print("{s}\n", .{long});
    try std.testing.expectEqual(long.len + 1, Cap.len);
}

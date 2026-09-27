//! The CLI's whole severity vocabulary: `error: ` and `note: `, word prefixes rather than emoji — the
//! convention grep, CI logs and the mcp/bot adapters parse. Everything goes through err()/note()
//! (logo.zig re-exports them), never a literal; the lint in app/lint.zig holds that line.
const std = @import("std");
const logo = @import("../logo.zig");
const palette = @import("palette.zig");

const Ansi = palette.Ansi;
const print = logo.print;
const setSink = logo.setSink;
const clearSink = logo.clearSink;
const init = logo.init;
const setAccent = logo.setAccent;

/// The `error: ` prefix — bold red on a colour terminal, plain elsewhere.
pub fn errPrefix() []const u8 {
    return if (logo.severityColor()) Ansi.red ++ "error: " ++ Ansi.reset else "error: ";
}

/// The `note: ` prefix — the live THEME accent on a colour terminal, via logo.accentSeq() so a note in the
/// scrollback re-tints. `error:` stays red: "this did not happen" should not move with the theme.
pub fn notePrefix() []const u8 {
    if (!logo.severityColor()) return "note: ";
    // Bold FIRST, then the accent: `error:` is bold red, so the two severities carry the same weight and
    // differ only in hue. In sentinel mode the bold in front survives the accent's expansion.
    const accent = logo.accentSeq();
    const reset = logo.colorSeq(Ansi.reset);
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

/// Print an `error: ` line (the message must supply its own trailing newline). Prefix and message
/// go out as ONE print, so a worker's line cannot land between them and tear the grammar.
pub fn err(comptime fmt: []const u8, args: anytype) void {
    print("{s}" ++ fmt, .{errPrefix()} ++ args);
}

/// Print a `note: ` line (the message must supply its own trailing newline), as one print.
pub fn note(comptime fmt: []const u8, args: anytype) void {
    print("{s}" ++ fmt, .{notePrefix()} ++ args);
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

test "err/note reach the sink as one line, and a print past 8 KB arrives whole" {
    const Count = struct {
        var calls: usize = 0;
        var len: usize = 0;
        var head: [16]u8 = undefined;
        fn sink(_: *anyopaque, bytes: []const u8) void {
            calls += 1;
            len = bytes.len;
            @memcpy(head[0..@min(16, bytes.len)], bytes[0..@min(16, bytes.len)]);
        }
    };
    var unused: u8 = 0;
    setSink(Count.sink, &unused);
    defer clearSink();
    defer init(false, false);
    init(false, false);
    Count.calls = 0;
    err("cannot read '{s}'\n", .{"a.png"});
    note("{d} left\n", .{3});
    try testing.expectEqual(@as(usize, 2), Count.calls);
    const long = "x" ** 20000;
    print("{s}\n", .{long});
    try testing.expectEqual(@as(usize, 3), Count.calls);
    try testing.expectEqual(long.len + 1, Count.len);
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

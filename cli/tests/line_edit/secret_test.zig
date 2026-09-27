//! `Editor.readSecret` driven over pipes: what is typed or pasted lands in the buffer and never on
//! the screen, a mouse report or an arrow is dropped, a lone Esc / Ctrl-C / a closed tty cancel —
//! and the piped console's read, which takes the next stdin line and zeroes it where it lay.
const std = @import("std");
const le = @import("../../src/line_edit/line_edit.zig");
const hooks = @import("../../src/console/hooks.zig");
const logo = @import("../../src/app/logo.zig");
const Capture = @import("../console/console_harness.zig").Capture;
const testing = std.testing;

const question = "Anthropic API key (input hidden): ";

/// Feed `keys` to a fresh editor's hidden read; returns what it read (null = cancelled) and
/// leaves everything it wrote to the terminal in `shown`.
fn readWith(keys: []const u8, out: []u8, shown: *std.ArrayList(u8)) !?[]const u8 {
    const in = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(in[0]);
    const term = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(term[0]);
    var ed = le.Editor{ .fd_in = in[0], .fd_out = term[1], .orig = std.mem.zeroes(std.posix.termios) };
    _ = std.c.write(in[1], keys.ptr, keys.len);
    _ = std.c.close(in[1]);
    const n = ed.readSecret(question, out);
    _ = std.c.close(term[1]);
    var buf: [4096]u8 = undefined;
    const got: usize = @intCast(std.c.read(term[0], &buf, buf.len));
    try shown.appendSlice(testing.allocator, buf[0..got]);
    return if (n) |len| out[0..len] else null;
}

test "readSecret: typed and edited, nothing of it echoed" {
    var shown: std.ArrayList(u8) = .empty;
    defer shown.deinit(testing.allocator);
    var out: [256]u8 = undefined;
    const got = (try readWith("sk-ant-abc\x7fd\r", &out, &shown)).?;
    try testing.expectEqualStrings("sk-ant-abd", got);
    try testing.expect(std.mem.indexOf(u8, shown.items, question) != null);
    try testing.expect(std.mem.indexOf(u8, shown.items, "(entered, hidden)") != null);
    try testing.expect(std.mem.indexOf(u8, shown.items, "sk-ant") == null);
}

test "readSecret: a bracketed paste lands whole; a mouse report and Ctrl-U are not text" {
    var shown: std.ArrayList(u8) = .empty;
    defer shown.deinit(testing.allocator);
    var out: [256]u8 = undefined;
    try testing.expectEqualStrings("sk-ant-pasted-123", (try readWith("\x1b[200~sk-ant-pasted-123\x1b[201~\r", &out, &shown)).?);
    try testing.expectEqualStrings("ok", (try readWith("\x1b[<0;10;5Mo\x1b[Ak\r", &out, &shown)).?);
    try testing.expectEqualStrings("new", (try readWith("old\x15new\r", &out, &shown)).?);
    try testing.expect(std.mem.indexOf(u8, shown.items, "pasted") == null);
}

test "readSecret: a lone Esc, Ctrl-C and a closed tty cancel" {
    var shown: std.ArrayList(u8) = .empty;
    defer shown.deinit(testing.allocator);
    var out: [256]u8 = undefined;
    try testing.expect((try readWith("sk\x1b", &out, &shown)) == null);
    try testing.expect((try readWith("sk\x03", &out, &shown)) == null);
    try testing.expect((try readWith("sk-never-ended", &out, &shown)) == null);
    try testing.expect(std.mem.indexOf(u8, shown.items, "(cancelled)") != null);
    try testing.expect(std.mem.indexOf(u8, shown.items, "sk") == null);
}

test "piped /llm key: the next line is the key, zeroed where the reader held it" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var data = "sk-ant-piped-42 \n/llm\n".*;
    var r = std.Io.Reader.fixed(&data);
    var pc = hooks.PipedConfirm{ .r = &r };
    var out: [64]u8 = undefined;
    const n = hooks.PipedConfirm.secret(&pc, question, &out).?;
    try testing.expectEqualStrings("sk-ant-piped-42", out[0..n]);
    try testing.expect(std.mem.indexOf(u8, &data, "sk-ant") == null);
    try testing.expectEqualStrings("/llm", (try r.takeDelimiter('\n')).?); // the next command is intact
    try testing.expect(std.mem.indexOf(u8, cap.text(), "sk-ant") == null);
}

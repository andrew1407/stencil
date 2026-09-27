//! The line editor's pictures, driven over pipes: image markers on Ctrl-V and Backspace, Ctrl-Z
//! taking one back, the fourth refused, an empty bracketed paste and clipboard text. No tty:
//! `readLine` only reads bytes.
const std = @import("std");
const le = @import("../../src/line_edit/line_edit.zig");
const logo = @import("../../src/app/logo.zig");
const screen_mod = @import("../../src/console/screen.zig");

const Editor = le.Editor;
const History = le.History;
const PendingImages = le.PendingImages;
const PasteResult = le.PasteResult;
const max_line = le.max_line;
const max_pending_images = le.max_pending_images;
const markerEnd = le.markerEnd;
const markerBefore = le.markerBefore;
const stripMarkers = le.stripMarkers;
const testing = std.testing;

test "image markers: found, deleted whole, and stripped off the submitted line" {
    const line = "look [Image #1 shot.png] at this";
    try testing.expectEqual(@as(?usize, 24), markerEnd(line, 5));
    try testing.expectEqual(@as(?usize, null), markerEnd(line, 0)); // not a marker start
    try testing.expectEqual(@as(?usize, null), markerEnd("[Image #9 x]", 0)); // index out of range
    try testing.expectEqual(@as(?usize, 5), markerBefore(line, 24)); // cursor right behind it
    try testing.expectEqual(@as(?usize, null), markerBefore(line, 23));

    var out: [max_line]u8 = undefined;
    try testing.expectEqualStrings("look at this", stripMarkers(&out, line));
    try testing.expectEqualStrings("/prompt describe", stripMarkers(&out, "/prompt [Image #1 a.png] describe"));
    try testing.expectEqualStrings("/upload", stripMarkers(&out, "/upload [Image #1 a.png] "));
    try testing.expectEqualStrings("", stripMarkers(&out, "[Image #1 a.png]"));
    // A line with no markers is handed back byte-for-byte (spacing included).
    try testing.expectEqualStrings("/crop  x1=1", stripMarkers(&out, "/crop  x1=1"));
}

// A stand-in host for the pending-image hooks: counts what it holds, no clipboard involved.
const MockPending = struct {
    held: usize = 0,
    last_kept: usize = 0,
    give_text: ?[]const u8 = null, // set to answer a Ctrl-V with clipboard TEXT instead

    fn hooks(self: *MockPending) PendingImages {
        return .{ .ctx = self, .paste = add, .addPath = addPath, .keep = keep, .count = count };
    }
    fn add(ctx: *anyopaque, before: []const u8, label: []u8, text: []u8) PasteResult {
        const self: *MockPending = @ptrCast(@alignCast(ctx));
        if (self.give_text) |t| {
            @memcpy(text[0..t.len], t);
            return .{ .text = t.len };
        }
        if (self.held >= max_pending_images or std.mem.startsWith(u8, before, "/upload")) return .none;
        self.held += 1;
        const name = "shot.png";
        @memcpy(label[0..name.len], name);
        return .{ .image = name.len };
    }
    fn addPath(_: *anyopaque, _: []const u8, _: []const u8, _: []u8) ?usize {
        return null; // this host never claims a pasted path
    }
    fn keep(ctx: *anyopaque, kept: []const usize) void {
        const self: *MockPending = @ptrCast(@alignCast(ctx));
        self.held = kept.len;
        self.last_kept = kept.len;
    }
    fn count(ctx: *anyopaque) usize {
        const self: *MockPending = @ptrCast(@alignCast(ctx));
        return self.held;
    }
};

test "Ctrl-V drops an image marker into the line; Backspace over it takes the picture back" {
    const in = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(in[0]);
    const out = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(out[0]);
    defer _ = std.c.close(out[1]);

    var mock = MockPending{};
    var ed = Editor{ .fd_in = in[0], .fd_out = out[1], .orig = std.mem.zeroes(std.posix.termios) };
    ed.pending = mock.hooks();
    var hist = History{ .gpa = testing.allocator };
    defer hist.deinit();
    var buf: [max_line]u8 = undefined;
    var armed = false;

    // "hi" Ctrl-V Enter — the chord no longer ends the line, it attaches to it.
    const typed = [_]u8{ 'h', 'i', 22, '\r' };
    _ = std.c.write(in[1], &typed, typed.len);
    const first = ed.readLine("> ", &buf, &hist, &.{}, &armed, "");
    try testing.expectEqualStrings("hi [Image #1 shot.png] ", buf[0..first.line]);
    try testing.expectEqual(@as(usize, 1), mock.held);

    // Backspace eats the trailing space, a second one the WHOLE marker — and the host is
    // told, so the picture behind it is dropped rather than orphaned.
    mock.held = 1;
    const del = [_]u8{ 22, 127, 127, '\r' };
    _ = std.c.write(in[1], &del, del.len);
    _ = std.c.close(in[1]);
    const second = ed.readLine("> ", &buf, &hist, &.{}, &armed, "");
    try testing.expectEqualStrings("", buf[0..second.line]);
    try testing.expectEqual(@as(usize, 0), mock.held);
}

test "with images pending, Ctrl-Z takes the last one back instead of leaving the line" {
    const in = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(in[0]);
    const out = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(out[0]);
    defer _ = std.c.close(out[1]);

    var mock = MockPending{};
    var ed = Editor{ .fd_in = in[0], .fd_out = out[1], .orig = std.mem.zeroes(std.posix.termios) };
    ed.pending = mock.hooks();
    var hist = History{ .gpa = testing.allocator };
    defer hist.deinit();
    var buf: [max_line]u8 = undefined;
    var armed = false;

    const keys = [_]u8{ 22, 22, 26, '\r' }; // two images, then un-paste one
    _ = std.c.write(in[1], &keys, keys.len);
    const res = ed.readLine("> ", &buf, &hist, &.{}, &armed, "");
    try testing.expectEqualStrings("[Image #1 shot.png] ", buf[0..res.line]);
    try testing.expectEqual(@as(usize, 1), mock.held);

    // With none left — the console drains the line's images as it runs it — Ctrl-Z is the
    // session's own `/unpaste` again.
    mock.held = 0;
    const z = [_]u8{26};
    _ = std.c.write(in[1], &z, z.len);
    _ = std.c.close(in[1]);
    try testing.expect(ed.readLine("> ", &buf, &hist, &.{}, &armed, "") == .unpaste);
}

test "a fourth image is refused rather than silently dropped" {
    const in = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(in[0]);
    const out = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(out[0]);
    defer _ = std.c.close(out[1]);
    var swallowed: u8 = 0;
    logo.setSink(struct {
        fn sink(_: *anyopaque, _: []const u8) void {}
    }.sink, &swallowed);
    defer logo.clearSink();

    var mock = MockPending{};
    var ed = Editor{ .fd_in = in[0], .fd_out = out[1], .orig = std.mem.zeroes(std.posix.termios) };
    ed.pending = mock.hooks();
    var hist = History{ .gpa = testing.allocator };
    defer hist.deinit();
    var buf: [max_line]u8 = undefined;
    var armed = false;

    const keys = [_]u8{ 22, 22, 22, 22, '\r' };
    _ = std.c.write(in[1], &keys, keys.len);
    _ = std.c.close(in[1]);
    const res = ed.readLine("> ", &buf, &hist, &.{}, &armed, "");
    try testing.expectEqual(@as(usize, max_pending_images), mock.held);
    try testing.expectEqual(@as(usize, 3), std.mem.count(u8, buf[0..res.line], "[Image #"));
}

test "a paste that delivered nothing takes the image off the clipboard instead" {
    // ⌘V with only an image copied: the terminal's paste event carries text, and there is
    // none — so the empty bracketed paste is the signal to go and read the picture.
    const in = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(in[0]);
    const out = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(out[0]);
    defer _ = std.c.close(out[1]);

    var mock = MockPending{};
    var ed = Editor{ .fd_in = in[0], .fd_out = out[1], .orig = std.mem.zeroes(std.posix.termios) };
    ed.pending = mock.hooks();
    var hist = History{ .gpa = testing.allocator };
    defer hist.deinit();
    var buf: [max_line]u8 = undefined;
    var armed = false;

    const keys = "ask \x1b[200~\x1b[201~\r"; // an empty bracketed paste mid-line
    _ = std.c.write(in[1], keys.ptr, keys.len);
    _ = std.c.close(in[1]);
    const res = ed.readLine("> ", &buf, &hist, &.{}, &armed, "");
    try testing.expectEqualStrings("ask [Image #1 shot.png] ", buf[0..res.line]);
    try testing.expectEqual(@as(usize, 1), mock.held);
}

test "Ctrl-V types the clipboard's TEXT when it holds no picture" {
    const in = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(in[0]);
    const out = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(out[0]);
    defer _ = std.c.close(out[1]);

    var mock = MockPending{ .give_text = "pasted words" };
    var ed = Editor{ .fd_in = in[0], .fd_out = out[1], .orig = std.mem.zeroes(std.posix.termios) };
    ed.pending = mock.hooks();
    var hist = History{ .gpa = testing.allocator };
    defer hist.deinit();
    var buf: [max_line]u8 = undefined;
    var armed = false;

    const keys = "/prompt say \x16\r";
    _ = std.c.write(in[1], keys.ptr, keys.len);
    _ = std.c.close(in[1]);
    const res = ed.readLine("> ", &buf, &hist, &.{}, &armed, "");
    try testing.expectEqualStrings("/prompt say pasted words", buf[0..res.line]);
    try testing.expectEqual(@as(usize, 0), mock.held); // text is typed, not held as an image
}

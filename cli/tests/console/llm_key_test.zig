//! The console's anthropic session key (llm-contract §5): `/llm key` asks with nothing echoed, the
//! key lands only in the config, a typed one is masked in the scrollback and the history, and it
//! goes when its TTL passes (a fake clock), on `/llm key forget`, or on a switch of provider.
const std = @import("std");
const console = @import("../../src/console.zig");
const llm = @import("../../src/llm.zig");
const logo = @import("../../src/app/logo.zig");
const line_edit = @import("../../src/line_edit/line_edit.zig");
const loop = @import("../../src/console/loop.zig");
const key = @import("../../src/console/llm/key.zig");
const Capture = @import("console_harness.zig").Capture;
const fx = @import("fx_harness.zig");
const testing = std.testing;

const typed_key = "sk-ant-test-console-0123456789";
const entered_key = "sk-ant-test-hidden-9876543210";

// A clock the test moves: the key's TTL runs on the Io's real clock.
var clock_vt: std.Io.VTable = undefined;
var clock_ms: i64 = 1_700_000_000_000;
fn clockNow(_: ?*anyopaque, _: std.Io.Clock) std.Io.Timestamp {
    return .{ .nanoseconds = @as(i96, clock_ms) * std.time.ns_per_ms };
}
fn fakeClock(base: std.Io) std.Io {
    clock_vt = base.vtable.*;
    clock_vt.now = clockNow;
    return .{ .userdata = base.userdata, .vtable = &clock_vt };
}

/// The hidden read the console installs, answering with `answer` and recording the question.
const Reader = struct {
    answer: []const u8,
    asked: usize = 0,
    question: [64]u8 = undefined,
    q_len: usize = 0,
    fn read(ctx: ?*anyopaque, question: []const u8, out: []u8) ?usize {
        const self: *Reader = @ptrCast(@alignCast(ctx.?));
        self.asked += 1;
        self.q_len = @min(question.len, self.question.len);
        @memcpy(self.question[0..self.q_len], question[0..self.q_len]);
        @memcpy(out[0..self.answer.len], self.answer);
        return self.answer.len;
    }
};

test "/llm key asks with the input hidden; the key reaches the config and nothing else" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = fakeClock(threaded.io());
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var reader = Reader{ .answer = entered_key };
    var session = console.Session{ .gpa = a, .secret_fn = Reader.read, .secret_ctx = &reader, .secret_tty = true };
    defer session.deinit();

    _ = try console.handle(&session, io, "/llm provider anthropic");
    try testing.expectEqualStrings("https://api.anthropic.com", session.llm_cfg.?.base_url);
    _ = try console.handle(&session, io, "/llm key");
    try testing.expectEqual(@as(usize, 1), reader.asked);
    try testing.expectEqualStrings("Anthropic API key (input hidden): ", reader.question[0..reader.q_len]);
    try testing.expectEqualStrings(entered_key, session.llm_cfg.?.api_key);
    try testing.expectEqual(clock_ms + llm.keyTtlMs(), session.llm_cfg.?.key_expires_ms);

    _ = try console.handle(&session, io, "/llm");
    try testing.expect(std.mem.indexOf(u8, cap.text(), "provider anthropic") != null);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "key:    key held until ") != null);
    try testing.expect(std.mem.indexOf(u8, cap.text(), entered_key) == null);
}

test "an expired key is dropped before the next request, which fails disabled and asks again" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = fakeClock(threaded.io());
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var reader = Reader{ .answer = entered_key };
    var session = console.Session{
        .gpa = a,
        .llm_env = .{ .provider = "anthropic", .base_url = "https://api.anthropic.test", .api_key = typed_key },
        .secret_fn = Reader.read,
        .secret_ctx = &reader,
        .secret_tty = true,
    };
    defer session.deinit();

    _ = try console.handle(&session, io, "/llm"); // the environment's key starts its clock here
    try testing.expectEqualStrings(typed_key, session.llm_cfg.?.api_key);
    clock_ms += llm.keyTtlMs() - 1;
    try testing.expect(!session.llm_cfg.?.expireKey(a, clock_ms)); // a millisecond to spare
    clock_ms += 1;

    _ = try console.handle(&session, io, "/prompt crop 10% off the left");
    const said = cap.text();
    try testing.expect(std.mem.indexOf(u8, said, "session time is up") != null);
    try testing.expect(std.mem.indexOf(u8, said, llm.no_key_message ++ "\n") != null);
    try testing.expect(std.mem.indexOf(u8, said, "HTTP") == null); // nothing was sent
    try testing.expectEqual(@as(usize, 1), reader.asked); // asked again at once
    try testing.expectEqualStrings(entered_key, session.llm_cfg.?.api_key);
    try testing.expectEqual(clock_ms + llm.keyTtlMs(), session.llm_cfg.?.key_expires_ms);
    try testing.expect(std.mem.indexOf(u8, said, "send the prompt again") != null);
}

test "/llm key forget, and a switch to or from anthropic, drop the key" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = fakeClock(threaded.io());
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = console.Session{ .gpa = a, .llm_env = .{ .provider = "anthropic" } };
    defer session.deinit();

    _ = try console.handle(&session, io, "/llm key " ++ typed_key);
    try testing.expectEqualStrings(typed_key, session.llm_cfg.?.api_key);
    _ = try console.handle(&session, io, "/llm key forget");
    try testing.expectEqualStrings("", session.llm_cfg.?.api_key);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "llm api key forgotten") != null);

    _ = try console.handle(&session, io, "/llm key " ++ typed_key);
    _ = try console.handle(&session, io, "/llm provider openai-compat");
    try testing.expectEqualStrings("", session.llm_cfg.?.api_key); // never sent to another endpoint
    try testing.expect(std.mem.indexOf(u8, cap.text(), "the API key was dropped") != null);
    try testing.expect(std.mem.indexOf(u8, cap.text(), typed_key) == null);
    // Nothing to read a key from (no terminal, no pipe): said, and the config untouched.
    _ = try console.handle(&session, io, "/llm key");
    try testing.expect(std.mem.indexOf(u8, cap.text(), "set STENCIL_LLM_API_KEY") != null);
}

test "a key typed after /llm key is masked in the full-screen scrollback and the history" {
    const a = testing.allocator;
    var rig: fx.Rig = undefined;
    try rig.start(a, 24, 100);
    defer rig.deinit();
    const in = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(in[0]);
    defer _ = std.c.close(in[1]);
    var ed = line_edit.Editor{ .fd_in = in[0], .fd_out = fx.fake_fd, .orig = std.mem.zeroes(std.posix.termios), .screen = &rig.scr };
    var hist = line_edit.History{ .gpa = a };
    defer hist.deinit();
    var sbuf: [line_edit.max_line]u8 = undefined;

    const typed = "/llm key " ++ typed_key;
    const line = loop.recordLine(&rig.session, &ed, &rig.scr, &hist, typed, &sbuf);
    try testing.expectEqualStrings(typed, line); // the command itself still runs with the key
    try testing.expectEqualStrings("/llm key", hist.items.items[hist.items.items.len - 1]);
    try testing.expect(std.mem.indexOf(u8, rig.scr.lines.back().?, key.masked_echo) != null);
    for (0..rig.scr.lines.len) |i| try testing.expect(std.mem.indexOf(u8, rig.scr.lines.at(i), typed_key) == null);
    try testing.expect(std.mem.indexOf(u8, rig.frame.items, typed_key) == null); // nor painted

    _ = loop.recordLine(&rig.session, &ed, &rig.scr, &hist, "/llm model claude-haiku-4-5", &sbuf);
    try testing.expectEqualStrings("/llm model claude-haiku-4-5", hist.items.items[hist.items.items.len - 1]);
}

test "on the plain terminal the typed key's row is repainted masked" {
    const a = testing.allocator;
    const in = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(in[0]);
    defer _ = std.c.close(in[1]);
    const out = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(out[0]);
    var ed = line_edit.Editor{ .fd_in = in[0], .fd_out = out[1], .orig = std.mem.zeroes(std.posix.termios) };
    var hist = line_edit.History{ .gpa = a };
    defer hist.deinit();
    var session = console.Session{ .gpa = a };
    defer session.deinit();
    var sbuf: [line_edit.max_line]u8 = undefined;

    _ = loop.recordLine(&session, &ed, null, &hist, "/llm key " ++ typed_key, &sbuf);
    _ = std.c.close(out[1]);
    var got: [4096]u8 = undefined;
    const n: usize = @intCast(std.c.read(out[0], &got, got.len));
    try testing.expect(std.mem.startsWith(u8, got[0..n], "\x1b[1A\r\x1b[J")); // up over the row, cleared
    try testing.expect(std.mem.indexOf(u8, got[0..n], key.masked_echo["/llm".len..]) != null); // the command word is coloured apart
    try testing.expect(std.mem.indexOf(u8, got[0..n], typed_key) == null);
    try testing.expectEqualStrings("/llm key", hist.items.items[0]);
}

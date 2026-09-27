//! The console's LLM key (llm-contract §5): asked for with the input hidden, held in memory
//! only — an anthropic key until its TTL passes, `/llm key forget`, or the console exits —
//! and never echoed: a key typed after `/llm key` is masked before the scrollback or the
//! history can keep it.
const std = @import("std");
const builtin = @import("builtin");
const logo = @import("../../app/logo.zig");
const llm = @import("../../llm.zig");
const commands = @import("../commands.zig");
const Session = @import("../session.zig").Session;

/// What the scrollback shows, and the history recalls, in place of a line that typed a key:
/// recalled, the bare form asks for the key again, hidden.
pub const masked_echo = "/llm key ••••••••";
pub const masked_history = "/llm key";

/// True for a line that types a key on the command line (`/llm key <value>`).
pub fn typesKey(line: []const u8) bool {
    const cmd = commands.parseCommand(line);
    if (commands.verbOf(cmd.word) != .llm) return false;
    return llm.parseCmd(cmd.arg) == .key;
}

/// The wall clock the key's TTL runs on, in ms (a test's Io fakes it).
pub fn nowMs(io: std.Io) i64 {
    return std.Io.Clock.now(.real, io).toMilliseconds();
}

/// `/llm key <value>`: hold the key typed on the line.
pub fn setKey(session: *Session, io: std.Io, cfg: *llm.Config, key: []const u8) !void {
    try cfg.setApiKey(session.gpa, key);
    cfg.armKey(nowMs(io));
    announce(cfg);
}

/// Bare `/llm key`: read the key with nothing echoed. False when there is no way to ask or
/// nothing was entered; the read buffer is zeroed either way.
pub fn promptKey(session: *Session, io: std.Io, cfg: *llm.Config) !bool {
    const read = session.secret_fn orelse {
        logo.err("nothing to read a key from here — set STENCIL_LLM_API_KEY\n", .{});
        return false;
    };
    var buf: [1024]u8 = undefined;
    defer std.crypto.secureZero(u8, &buf);
    const q = if (cfg.provider == .anthropic) "Anthropic API key (input hidden): " else "API key (input hidden): ";
    const n = read(session.secret_ctx, q, &buf) orelse 0;
    const key = std.mem.trim(u8, buf[0..n], " \t\r\n");
    if (key.len == 0) {
        logo.print("no key entered — the key is unchanged\n", .{});
        return false;
    }
    try setKey(session, io, cfg, key);
    return true;
}

/// `/llm key forget`: zero the key and drop it now.
pub fn forget(session: *Session, cfg: *llm.Config) void {
    const had = cfg.api_key.len != 0;
    cfg.forgetKey(session.gpa);
    logo.print("{s}\n", .{if (had) "llm api key forgotten" else "no llm api key to forget"});
}

/// Before a request: an anthropic key whose time is up is zeroed and dropped, and said so.
pub fn dropExpired(session: *Session, io: std.Io, cfg: *llm.Config) void {
    if (cfg.expireKey(session.gpa, nowMs(io))) logo.note("the Anthropic API key's session time is up — it was dropped\n", .{});
}

/// After a turn failed for want of a key: a person at the keyboard is asked for it at once; a
/// piped console is told how. A one-shot has no console to ask on and says its own hint.
pub fn askAgain(session: *Session, io: std.Io, cfg: *llm.Config) !void {
    if (session.secret_fn == null) return;
    if (!session.secret_tty) return logo.note("'/llm key' enters the key — the input is hidden\n", .{});
    if (try promptKey(session, io, cfg)) logo.print("send the prompt again to use it\n", .{});
}

/// The `/llm` listing's key row.
pub fn status(cfg: *const llm.Config, buf: *[64]u8) []const u8 {
    if (cfg.provider != .anthropic) return if (cfg.api_key.len != 0) "(set, hidden)" else "(not set)";
    if (cfg.api_key.len == 0) return "no key — '/llm key' enters one, hidden";
    var cb: ClockBuf = undefined;
    return std.fmt.bufPrint(buf, "key held until {s}", .{clock(cfg.key_expires_ms, &cb)}) catch "key held";
}

fn announce(cfg: *const llm.Config) void {
    if (cfg.provider != .anthropic) return logo.print("llm api key set (hidden)\n", .{});
    var cb: ClockBuf = undefined;
    logo.print("llm api key held until {s} (hidden; '/llm key forget' drops it)\n", .{clock(cfg.key_expires_ms, &cb)});
}

pub const ClockBuf = [16]u8;

/// `ms` on the local wall clock as HH:MM — or in UTC, said so, where the C library cannot tell.
pub fn clock(ms: i64, buf: *ClockBuf) []const u8 {
    const secs = @divFloor(ms, std.time.ms_per_s);
    const local = if (builtin.os.tag == .windows) null else localHourMinute(secs);
    if (local) |hm| return std.fmt.bufPrint(buf, "{d:0>2}:{d:0>2}", .{ hm[0], hm[1] }) catch "";
    const day = @mod(secs, std.time.s_per_day);
    return std.fmt.bufPrint(buf, "{d:0>2}:{d:0>2} UTC", .{ @divFloor(day, 3600), @divFloor(@mod(day, 3600), 60) }) catch "";
}

// POSIX's leading nine `struct tm` ints; the tail pads past every platform's extra fields.
const Tm = extern struct { sec: c_int, min: c_int, hour: c_int, rest: [6]c_int, tail: [8]u64 };
extern "c" fn localtime_r(t: *const std.c.time_t, out: *Tm) ?*Tm;

fn localHourMinute(secs: i64) ?[2]u8 {
    const t: std.c.time_t = @intCast(secs);
    var tm: Tm = undefined;
    _ = localtime_r(&t, &tm) orelse return null;
    return .{ @intCast(tm.hour), @intCast(tm.min) };
}

const testing = std.testing;

test "typesKey: only a key typed after /llm key is masked" {
    try testing.expect(typesKey("/llm key sk-ant-123"));
    try testing.expect(typesKey("  llm apikey sk-ant-123 "));
    try testing.expect(!typesKey("/llm key")); // the hidden prompt
    try testing.expect(!typesKey("/llm key forget"));
    try testing.expect(!typesKey("/llm model claude"));
    try testing.expect(!typesKey("/prompt llm key x"));
}

test "clock: two-digit hours and minutes" {
    var b: ClockBuf = undefined;
    const s = clock(1_700_000_000_000, &b);
    try testing.expect(s.len >= 5 and s[2] == ':');
    for ([_]usize{ 0, 1, 3, 4 }) |i| try testing.expect(std.ascii.isDigit(s[i]));
}

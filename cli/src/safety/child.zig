//! Child processes (ffmpeg, the clipboard helpers) spawned WITHOUT the user's credentials:
//! `STENCIL_LLM_API_KEY` and its siblings, and the server tokens — nothing the CLI execs has
//! any business inheriting them, and each one under a deadline that kills it.
const std = @import("std");
const builtin = @import("builtin");
const report = @import("../app/report.zig");

/// ms an ffmpeg frame grab or ffprobe read may run: a remote clip is already a local file by
/// then, so this bounds decoding alone.
pub const media_timeout_ms: u32 = 60_000;
/// ms a clipboard helper (osascript, pbcopy, wl-paste, xclip, powershell) may run.
pub const helper_timeout_ms: u32 = 10_000;

const llm_prefix = "STENCIL_LLM_";
const server_token_keys = [_][]const u8{ "STENCIL_SERVER_TOKEN", "STENCIL_SERVER_TOKENS" };

/// The parent environment, captured once by main(). Null in tests and other embedders, and
/// then a child simply inherits the process environment as `std.process.run` would.
var parent_env: ?*const std.process.Environ.Map = null;

pub fn captureEnv(map: *const std.process.Environ.Map) void {
    parent_env = map;
}

pub const RunError = std.process.RunError || std.mem.Allocator.Error || error{TimedOut};

/// `std.process.run`, with the credentials taken out of the child's environment and the child
/// killed once `timeout_ms` (0 = none) has passed; `TimedOut` has already said so.
pub fn run(
    gpa: std.mem.Allocator,
    io: std.Io,
    options: std.process.RunOptions,
    timeout_ms: u32,
) RunError!std.process.RunResult {
    var env = try scrubbedEnv(gpa);
    defer if (env) |*m| m.deinit();
    var opts = options;
    if (env) |*m| opts.environ_map = m;
    if (timeout_ms != 0) {
        const budget: std.Io.Clock.Duration = .{ .raw = .fromMilliseconds(timeout_ms), .clock = .awake };
        opts.timeout = .{ .deadline = .fromNow(io, budget) };
    }
    return std.process.run(gpa, io, opts) catch |e| {
        if (e != error.Timeout) return e;
        const tool = std.fs.path.basename(options.argv[0]);
        report.err("{s} did not finish within {d}s — stopped\n", .{ tool, (timeout_ms + 999) / 1000 });
        return error.TimedOut;
    };
}

/// A copy of the captured environment without the credentials; null when none was captured.
fn scrubbedEnv(gpa: std.mem.Allocator) !?std.process.Environ.Map {
    const src = parent_env orelse return null;
    var map: std.process.Environ.Map = .init(gpa);
    errdefer map.deinit();
    for (src.keys(), src.values()) |k, v| {
        if (std.ascii.startsWithIgnoreCase(k, llm_prefix) or isServerToken(k)) continue;
        try map.put(k, v);
    }
    return map;
}

fn isServerToken(key: []const u8) bool {
    for (server_token_keys) |k| if (std.ascii.eqlIgnoreCase(key, k)) return true;
    return false;
}

const testing = std.testing;

test "scrubbedEnv drops every STENCIL_LLM_* key and the server tokens, and keeps the rest" {
    const a = testing.allocator;
    var env: std.process.Environ.Map = .init(a);
    defer env.deinit();
    try env.put("PATH", "/usr/bin");
    try env.put("STENCIL_LLM_API_KEY", "sk-secret");
    try env.put("STENCIL_LLM_SERVER_TOKEN", "tok");
    try env.put("stencil_llm_base_url", "http://localhost:1234/v1"); // Windows keys fold case
    try env.put("STENCIL_SERVER_TOKEN", "srv");
    try env.put("STENCIL_SERVER_TOKENS", "http://h=srv");
    try env.put("STENCIL_SERVER_URL", "http://h"); // not a credential

    captureEnv(&env);
    defer parent_env = null;
    var scrubbed = (try scrubbedEnv(a)).?;
    defer scrubbed.deinit();

    try testing.expectEqualStrings("/usr/bin", scrubbed.get("PATH").?);
    try testing.expect(scrubbed.get("STENCIL_LLM_API_KEY") == null);
    try testing.expect(scrubbed.get("STENCIL_LLM_SERVER_TOKEN") == null);
    try testing.expect(scrubbed.get("stencil_llm_base_url") == null);
    try testing.expect(scrubbed.get("STENCIL_SERVER_TOKEN") == null);
    try testing.expect(scrubbed.get("STENCIL_SERVER_TOKENS") == null);
    try testing.expectEqual(@as(usize, 2), scrubbed.keys().len);
}

test "no captured environment means no replacement map (plain inheritance)" {
    parent_env = null;
    try testing.expect((try scrubbedEnv(testing.allocator)) == null);
}

test "a child past its deadline is killed and reported, not waited on" {
    if (builtin.os.tag == .windows) return error.SkipZigTest;
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const Sink = struct {
        var line: [128]u8 = undefined;
        var len: usize = 0;
        fn emit(_: *anyopaque, _: report.Severity, msg: []const u8) void {
            len = @min(msg.len, line.len);
            @memcpy(line[0..len], msg[0..len]);
        }
    };
    var ctx: u8 = 0;
    report.install(.{ .ctx = &ctx, .emitFn = Sink.emit });
    defer report.uninstall();

    const start = std.Io.Clock.awake.now(io);
    try testing.expectError(error.TimedOut, run(a, io, .{ .argv = &.{ "/bin/sleep", "30" } }, 100));
    const took = start.durationTo(std.Io.Clock.awake.now(io)).toMilliseconds();
    try testing.expect(took < 10_000);
    try testing.expectEqualStrings("sleep did not finish within 1s — stopped\n", Sink.line[0..Sink.len]);

    const ok = try run(a, io, .{ .argv = &.{ "/bin/sleep", "0" } }, 5_000);
    defer a.free(ok.stdout);
    defer a.free(ok.stderr);
    try testing.expectEqual(std.process.Child.Term{ .exited = 0 }, ok.term);
}

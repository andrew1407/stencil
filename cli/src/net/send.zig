//! The wire under net.zig's guard: one exchange, the 64 MiB body cap and the per-request
//! deadline. std.http.Client takes no timeout, so a deadlined request runs as a concurrent
//! task and is CANCELLED once its budget is gone — Io interrupts the blocked syscall, so a
//! silent host costs one deadline instead of a worker wedged forever.
const std = @import("std");
const report = @import("../app/report.zig");
const fetchPool = @import("fetchPool.zig");

pub const Error = error{ HttpFailed, BlockedHost, TimedOut };

/// Hard cap on the bytes read from a single fetch: bounds memory against a host streaming an endless
/// body, which matters for scrape's many untrusted URLs. Page-allocated, so a small response pays less.
pub const MAX_FETCH_BYTES = 64 << 20; // 64 MiB

/// ms one exchange may take end to end — connect, TLS, request, body. Generous for a big
/// image over a slow link, short enough that a stalling host never owns the prompt thread.
pub const DEFAULT_TIMEOUT_MS: u32 = 30_000;

pub const RequestOptions = struct {
    /// null lets std.http.Client infer it (GET without a payload, POST with one).
    method: ?std.http.Method = null,
    payload: ?[]const u8 = null,
    extra_headers: []const std.http.Header = &.{},
    /// Block loopback in addition to the always-blocked internal ranges — pass true for
    /// sub-resource URLs harvested from untrusted scanned content, false for user-named URLs.
    strict: bool = false,
    /// Skip the host/DNS block entirely (see isBlockedFetchHost: the server-connect path is
    /// exempt — the user names their own server). The cap and redirect refusal still apply.
    allow_named_host: bool = false,
    /// ms, 0 = wait forever. The LLM path passes its own, longer budget
    /// (llm.request_timeout_ms = providers.json timeouts.perSurface.cli.chatSeconds).
    timeout_ms: u32 = DEFAULT_TIMEOUT_MS,
};

pub const Response = struct {
    status: u16,
    body: []u8, // owned by the caller (present for non-2xx statuses too)
};

pub const Result = (Error || error{OutOfMemory})!Response;

/// One exchange on THIS thread: the host guard has already run and nothing bounds the wait.
/// `muted` is set while the waiting side is giving up, whose own message already said why.
pub fn once(
    gpa: std.mem.Allocator,
    io: std.Io,
    url: []const u8,
    opts: RequestOptions,
    muted: ?*const std.atomic.Value(bool),
) Result {
    var client: std.http.Client = .{ .allocator = gpa, .io = io };
    defer client.deinit();

    // Bounded scratch: a fixed writer returns error.WriteFailed once the body exceeds the cap, aborting
    // the stream instead of growing memory. Page-allocated, so a small response commits its own pages.
    const scratch = fetchPool.bodyScratch(MAX_FETCH_BYTES) orelse return Error.HttpFailed;
    var body: std.Io.Writer = .fixed(scratch);

    const result = client.fetch(.{
        .location = .{ .url = url },
        .method = opts.method,
        .payload = opts.payload,
        .extra_headers = opts.extra_headers,
        .response_writer = &body,
        // Refuse redirects: a public first hop must not 30x-bounce to an internal
        // host, which would slip past the pre-fetch host check in net.zig.
        .redirect_behavior = .not_allowed,
    }) catch |e| {
        if (muted == null or !muted.?.load(.acquire)) {
            if (e == error.WriteFailed) {
                report.err("response from {s} exceeds the {d}-byte fetch cap\n", .{ url, MAX_FETCH_BYTES });
            } else {
                report.err("HTTP request failed for {s}: {s}\n", .{ url, @errorName(e) });
            }
        }
        return Error.HttpFailed;
    };

    return .{
        .status = @intFromEnum(result.status),
        .body = try gpa.dupe(u8, body.buffered()),
    };
}

/// One exchange in flight on a concurrent task, so the waiting side can stop it.
const Task = struct {
    gpa: std.mem.Allocator,
    io: std.Io,
    url: []const u8,
    opts: RequestOptions,
    muted: std.atomic.Value(bool) = .init(false),
    done: std.Io.Event = .unset,
    out: Result = Error.HttpFailed,

    fn run(self: *Task) void {
        self.out = once(self.gpa, self.io, self.url, self.opts, &self.muted);
        self.done.set(self.io);
    }
};

/// `once` under `opts.timeout_ms`. Falls back to an unbounded call on this thread only where
/// the Io cannot spawn — there is then nothing left to wait on the exchange's behalf.
pub fn deadlined(gpa: std.mem.Allocator, io: std.Io, url: []const u8, opts: RequestOptions) Result {
    if (opts.timeout_ms == 0) return once(gpa, io, url, opts, null);
    var task = Task{ .gpa = gpa, .io = io, .url = url, .opts = opts };
    var fut = io.concurrent(Task.run, .{&task}) catch return once(gpa, io, url, opts, null);
    const budget: std.Io.Clock.Duration = .{ .raw = .fromMilliseconds(opts.timeout_ms), .clock = .awake };
    const deadline: std.Io.Clock.Timestamp = .fromNow(io, budget);
    while (!task.done.isSet()) {
        task.done.waitTimeout(io, .{ .deadline = deadline }) catch |e| {
            // Timeout also fires on a spurious wakeup, so the clock has the last word;
            // Canceled is our own caller giving up, and ends the wait at once.
            if (e == error.Timeout and deadline.durationFromNow(io).raw.nanoseconds > 0) continue;
            return abandon(&task, &fut, e == error.Timeout);
        };
    }
    fut.await(io);
    return task.out;
}

/// Stop a task, drop whatever it managed to buy, and say so — unless our own caller cancelled
/// us, in which case the answer is nobody's news.
fn abandon(task: *Task, fut: *std.Io.Future(void), timed_out: bool) Result {
    task.muted.store(true, .release);
    fut.cancel(task.io);
    if (task.out) |res| task.gpa.free(res.body) else |_| {}
    if (!timed_out) return Error.HttpFailed;
    report.err("no answer from {s} within {d}s\n", .{ task.url, task.opts.timeout_ms / 1000 });
    return Error.TimedOut;
}

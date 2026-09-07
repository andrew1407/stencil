//! Transport for the console LLM assistant: the background POST job + waiter,
//! the guarded net.request path, and the sanitized provider error details.
const std = @import("std");
const net = @import("../net.zig");
const logo = @import("../logo.zig");
const wire = @import("wire.zig");

// Symbols living in the sibling llm/ modules (facade: ../llm.zig).
const member = wire.member;
const memberStr = wire.memberStr;

// ── Transport ────────────────────────────────────────────────────────────────

/// `LlmDisabled` = the server's 503 llmDisabled (no LLM key configured) — typed so a
/// caller can tell "configure the server" apart from a broken transport (browser
/// `disabled` kind parity). Every other non-2xx is `HttpFailed`.
pub const PostError = error{ BlockedHost, HttpFailed, LlmDisabled, OutOfMemory, Cancelled, TimedOut };

/// How long one LLM call may run before it is abandoned (a vision plan over a big image is
/// slow, but a provider that never answers must not wedge the console forever). 600s is the
/// cli's documented outlier in providers.json (timeouts.perSurface.cli), kept deliberately.
pub const request_timeout_ms: i64 = 10 * 60 * 1000;

/// How a caller waiting on a slow call watches for a Ctrl-C. `poll` waits up to its
/// `timeout_ms` and returns true when the user asked to cancel; null = nothing to watch (the
/// one-shot CLI, tests), and then the call simply runs to completion on this thread.
pub const Waiter = struct {
    ctx: ?*anyopaque = null,
    poll: ?*const fn (ctx: *anyopaque, timeout_ms: i32) bool = null,
    timeout_ms: i64 = request_timeout_ms,
    // Fired once per wait beat with the clock in ms — the console's spinner advances on it.
    beat_ctx: ?*anyopaque = null,
    beat: ?*const fn (ctx: *anyopaque, now_ms: i64) void = null,

    fn watching(self: Waiter) bool {
        return self.poll != null and self.ctx != null;
    }
};

/// One in-flight POST handed to a worker thread. The state word is the ownership handoff:
/// whoever moves it out of `.running` owns the response — the worker frees it when the
/// caller has already given up, the caller collects it otherwise.
pub const Job = struct {
    const State = enum(u8) { running, finished, cancelled };

    gpa: std.mem.Allocator,
    io: std.Io,
    // Owned copies: the caller's buffers may be gone by the time an abandoned worker lands.
    url: []u8,
    auth: ?[]u8,
    body: []u8,
    state: std.atomic.Value(u8) = .init(@intFromEnum(State.running)),
    res: ?net.Response = null,
    err: ?PostError = null,

    fn init(gpa: std.mem.Allocator, io: std.Io, url: []const u8, auth: ?[]const u8, body: []const u8) !*Job {
        const job = try gpa.create(Job);
        errdefer gpa.destroy(job);
        job.* = .{
            .gpa = gpa,
            .io = io,
            .url = try gpa.dupe(u8, url),
            .auth = if (auth) |a| try gpa.dupe(u8, a) else null,
            .body = try gpa.dupe(u8, body),
        };
        return job;
    }

    /// Free the job itself (never the response — that follows the state handoff).
    fn destroy(self: *Job) void {
        const gpa = self.gpa;
        gpa.free(self.url);
        if (self.auth) |a| gpa.free(a);
        gpa.free(self.body);
        gpa.destroy(self);
    }

    /// Move the state out of `.running`, reporting whether this caller won the handoff.
    fn claim(self: *Job, to: State) bool {
        return self.state.cmpxchgStrong(
            @intFromEnum(State.running),
            @intFromEnum(to),
            .acq_rel,
            .acquire,
        ) == null;
    }

    fn run(self: *Job) void {
        if (rawRequest(self.gpa, self.io, self.url, self.auth, self.body)) |res| {
            self.res = res;
        } else |e| {
            self.err = e;
        }
        if (self.claim(.finished)) return; // the caller is still waiting — it collects
        self.discard(); // abandoned: this thread owns everything now
    }

    /// Throw away an abandoned job: whoever LOST the handoff calls this (the worker after a
    /// cancel; the caller's own cleanup when no worker ever ran).
    fn discard(self: *Job) void {
        if (self.res) |res| self.gpa.free(res.body);
        self.destroy();
    }
};

/// Wait for `job` while watching for a Ctrl-C and the deadline. Returns the response when the
/// worker landed first; `Cancelled`/`TimedOut` when this side won the handoff (the worker then
/// owns and frees the job, so it must not be touched again).
pub fn waitForJob(job: *Job, io: std.Io, waiter: Waiter) PostError!net.Response {
    const started = std.Io.Clock.now(.awake, io).toMilliseconds();
    while (job.state.load(.acquire) == @intFromEnum(Job.State.running)) {
        if (waiter.poll.?(waiter.ctx.?, wait_beat_ms)) {
            if (job.claim(.cancelled)) {
                // The user asked for the stop, so it is a note, not an `error:` — the command
                // did exactly what was asked. The deadline below still is an error.
                logo.note("cancelled — the assistant turn was stopped\n", .{});
                return PostError.Cancelled;
            }
            break; // it landed in the same instant: use the answer we already paid for
        }
        const now = std.Io.Clock.now(.awake, io).toMilliseconds();
        if (waiter.beat) |b| if (waiter.beat_ctx) |c| b(c, now);
        if (waiter.timeout_ms > 0 and now - started > waiter.timeout_ms) {
            if (job.claim(.cancelled)) {
                logo.err("the LLM endpoint did not answer within {d}s\n", .{@divTrunc(waiter.timeout_ms, 1000)});
                return PostError.TimedOut;
            }
            break;
        }
    }
    if (job.err) |e| return e;
    return job.res.?;
}

/// POST a JSON body and return the owned response bytes, over net.request's guarded path
/// (SSRF checks, redirect refusal, size cap) in NON-strict mode: LLM endpoints are
/// user-named, so loopback is allowed while private/metadata ranges stay blocked.
pub fn postJson(
    gpa: std.mem.Allocator,
    io: std.Io,
    url: []const u8,
    auth: ?[]const u8,
    body: []const u8,
    waiter: Waiter,
) PostError![]u8 {
    if (!waiter.watching()) return finish(gpa, try rawRequest(gpa, io, url, auth, body));
    // Watched: the request runs on a worker while this thread keeps reading the tty, so a
    // Ctrl-C (or the deadline) ends the wait instead of the console sitting deaf for minutes.
    const job = Job.init(gpa, io, url, auth, body) catch return PostError.OutOfMemory;
    var thread = std.Thread.spawn(.{}, Job.run, .{job}) catch {
        // No thread to spare — fall back to the plain blocking call rather than failing.
        job.destroy();
        return finish(gpa, try rawRequest(gpa, io, url, auth, body));
    };
    thread.detach();
    const res = try waitForJob(job, io, waiter);
    // The worker landed first and left the result to us; the job itself is ours to free.
    defer job.destroy();
    return finish(gpa, res);
}

/// How long one wait beat blocks on the tty — short enough that Ctrl-C feels immediate.
const wait_beat_ms: i32 = 60;

/// Turn a raw response into the reply body, reporting a non-2xx the §6.3 way.
fn finish(gpa: std.mem.Allocator, res: net.Response) PostError![]u8 {
    if (res.status < 200 or res.status >= 300) {
        defer gpa.free(res.body);
        // The reason ONCE (contract §6.3): the provider's own message when it has one —
        // the console already printed which endpoint it is asking — else the bare status.
        var buf: DetailBuf = undefined;
        const why = errorDetail(gpa, res.body, &buf);
        if (why.len != 0) {
            logo.err("{s}\n", .{why});
        } else {
            logo.err("the LLM endpoint answered HTTP {d}\n", .{res.status});
        }
        // Same printed message, typed: the server's llmDisabled is its own error.
        return if (isLlmDisabled(gpa, res.body)) PostError.LlmDisabled else PostError.HttpFailed;
    }
    return res.body;
}

/// True when a non-2xx body is the server's `{"code":"llmDisabled"}` (LLM not
/// configured) — the typed `LlmDisabled` seam, keyed on the code like every client.
pub fn isLlmDisabled(gpa: std.mem.Allocator, body: []const u8) bool {
    var parsed = std.json.parseFromSlice(std.json.Value, gpa, body, .{}) catch return false;
    defer parsed.deinit();
    const code = memberStr(parsed.value, "code") orelse return false;
    return std.mem.eql(u8, code, "llmDisabled");
}

/// The POST itself, with no waiting or reporting — runs on whichever thread calls it.
fn rawRequest(gpa: std.mem.Allocator, io: std.Io, url: []const u8, auth: ?[]const u8, body: []const u8) PostError!net.Response {
    var headers: [2]std.http.Header = undefined;
    var n: usize = 0;
    headers[n] = .{ .name = "content-type", .value = "application/json" };
    n += 1;
    if (auth) |a| {
        headers[n] = .{ .name = "authorization", .value = a };
        n += 1;
    }

    return net.request(gpa, io, url, .{
        .method = .POST,
        .payload = body,
        .extra_headers = headers[0..n],
    });
}

/// How much of a provider's own prose an error may quote (server upstream.go parity).
const detail_limit = 200;
const DetailBuf = [detail_limit + "…".len]u8;

/// The provider's own message from a non-2xx body (the three §6 error shapes), sanitized
/// by `sanitizeDetail`; empty when the body carries nothing usable. The raw body is
/// NEVER printed.
pub fn errorDetail(gpa: std.mem.Allocator, body: []const u8, out: *DetailBuf) []const u8 {
    var parsed = std.json.parseFromSlice(std.json.Value, gpa, body, .{}) catch return "";
    defer parsed.deinit();
    const root = parsed.value;
    const raw = memberStr(root, "message") orelse blk: {
        const e = member(root, "error") orelse return "";
        break :blk switch (e) {
            .string => |s| s,
            .object => memberStr(e, "message") orelse return "",
            else => return "",
        };
    };
    return sanitizeDetail(raw, out);
}

/// Untrusted provider prose made safe to print: control chars/newlines out (no forged
/// console lines), URL- and token-shaped words redacted (an endpoint may echo the key
/// back), whitespace collapsed, cut at `detail_limit` on a word boundary.
pub fn sanitizeDetail(text: []const u8, out: *DetailBuf) []const u8 {
    const src = text[0..@min(text.len, 4 * detail_limit)];
    var n: usize = 0;
    var i: usize = 0;
    while (nextWord(src, &i)) |w| {
        var word: []const u8 = if (secretish(w)) "[redacted]" else w;
        // Browser parity: "bearer <cred>"/"basic <cred>" collapses to ONE [redacted].
        if (isAuthScheme(w)) {
            var j = i;
            if (nextWord(src, &j)) |cred| {
                if (credRun(cred) >= 8) {
                    word = "[redacted]";
                    i = j;
                }
            }
        }
        const sep: usize = if (n == 0) 0 else 1;
        if (n + sep + word.len > detail_limit) {
            @memcpy(out[n..][0.."…".len], "…");
            return out[0 .. n + "…".len];
        }
        if (sep == 1) {
            out[n] = ' ';
            n += 1;
        }
        @memcpy(out[n..][0..word.len], word);
        n += word.len;
    }
    return out[0..n];
}

/// Word separators: spaces and every control byte (DEL included).
fn isDetailSep(c: u8) bool {
    return c <= ' ' or c == 0x7f;
}

/// The next separator-delimited word from `src`, advancing `i` past it; null at the end.
fn nextWord(src: []const u8, i: *usize) ?[]const u8 {
    while (i.* < src.len and isDetailSep(src[i.*])) i.* += 1;
    const start = i.*;
    while (i.* < src.len and !isDetailSep(src[i.*])) i.* += 1;
    return if (i.* == start) null else src[start..i.*];
}

/// The browser sanitizer's auth-scheme words: a following credential is redacted.
fn isAuthScheme(word: []const u8) bool {
    return std.ascii.eqlIgnoreCase(word, "bearer") or std.ascii.eqlIgnoreCase(word, "basic");
}

/// Leading run of the browser's bearer-credential chars (`A-Za-z0-9._~+/=-`).
fn credRun(word: []const u8) usize {
    for (word, 0..) |c, k| {
        if (!std.ascii.isAlphanumeric(c) and std.mem.indexOfScalar(u8, "._~+/=-", c) == null) return k;
    }
    return word.len;
}

/// Leading run of token chars (`A-Za-z0-9._-`).
fn tokenRun(word: []const u8) usize {
    for (word, 0..) |c, k| {
        if (!std.ascii.isAlphanumeric(c) and c != '.' and c != '_' and c != '-') return k;
    }
    return word.len;
}

/// True for a word that must not be echoed: an absolute URL (an internal endpoint is
/// not the user's business), a long opaque run, or a credential pair — one of the
/// browser's key heads (`sk|pk|api[-_]?key|key|token|secret`) + `-_=:` + a 6+ token run.
fn secretish(word: []const u8) bool {
    if (std.mem.indexOf(u8, word, "://") != null) return true;
    if (word.len >= 24 and isTokenChars(word)) return true;
    const names = [_][]const u8{ "api_key", "api-key", "apikey", "sk", "pk", "key", "token", "secret" };
    for (names) |name| {
        if (word.len < name.len + 1 + 6) continue;
        if (!std.ascii.startsWithIgnoreCase(word, name)) continue;
        if (std.mem.indexOfScalar(u8, "-_=:", word[name.len]) == null) continue;
        if (tokenRun(word[name.len + 1 ..]) >= 6) return true;
    }
    return false;
}

/// True when every byte is a token character (`A-Za-z0-9._-`).
fn isTokenChars(s: []const u8) bool {
    for (s) |c| {
        if (!std.ascii.isAlphanumeric(c) and c != '.' and c != '_' and c != '-') return false;
    }
    return true;
}

/// How much of a response body is quoted in error messages.
pub const clip_limit = 300;
pub const ClipBuf = [clip_limit + "…".len]u8;

/// Bound a response body for inclusion in a message (≤ `clip_limit` bytes, backed off to a
/// UTF-8 codepoint boundary, with an ellipsis). Returns a slice of `buf` or of `body` itself.
pub fn clip(buf: *ClipBuf, body: []const u8) []const u8 {
    const t = std.mem.trim(u8, body, " \t\r\n");
    if (t.len <= clip_limit) return t;
    var end: usize = clip_limit;
    while (end > 0 and (t[end] & 0xC0) == 0x80) : (end -= 1) {}
    @memcpy(buf[0..end], t[0..end]);
    const ell = "…";
    @memcpy(buf[end .. end + ell.len], ell);
    return buf[0 .. end + ell.len];
}

// ── tests ────────────────────────────────────────────────────────────────────

const testing = std.testing;

test "errorDetail: the provider's reason, said once and never the raw body" {
    const a = testing.allocator;
    var buf: DetailBuf = undefined;
    // stencil-server {code,message}: the reason alone — no status, no upstream prose.
    try testing.expectEqualStrings(
        "the LLM provider is out of credits or has no active billing",
        errorDetail(a, "{\"code\":\"llmUpstream\",\"message\":\"the LLM provider is out of credits or has no active billing\"}", &buf),
    );
    // ollama {"error":"…"} and openai-compat {"error":{"message":"…"}}.
    try testing.expectEqualStrings("model 'x' not found", errorDetail(a, "{\"error\":\"model 'x' not found\"}", &buf));
    try testing.expectEqualStrings("invalid model", errorDetail(a, "{\"error\":{\"message\":\"invalid model\"}}", &buf));
    // Nothing usable (or not JSON at all) quotes nothing — the caller prints the status.
    try testing.expectEqualStrings("", errorDetail(a, "<html>gateway down</html>", &buf));
    try testing.expectEqualStrings("", errorDetail(a, "{\"detail\":\"x\"}", &buf));
}

test "isLlmDisabled: only the server's llmDisabled code types as disabled" {
    const a = testing.allocator;
    try testing.expect(isLlmDisabled(a, "{\"code\":\"llmDisabled\",\"message\":\"LLM is not configured on this server\"}"));
    try testing.expect(!isLlmDisabled(a, "{\"code\":\"llmUpstream\",\"message\":\"out of credits\"}"));
    try testing.expect(!isLlmDisabled(a, "{\"error\":\"model not found\"}"));
    try testing.expect(!isLlmDisabled(a, "<html>gateway down</html>"));
}

test "sanitizeDetail: bounded, control-free, and never echoing a key or URL" {
    var buf: DetailBuf = undefined;
    try testing.expectEqualStrings("model not found", sanitizeDetail("model\nnot\tfound", &buf));
    try testing.expectEqualStrings(
        "Incorrect API key provided: [redacted]",
        sanitizeDetail("Incorrect API key provided: sk-abcdef1234567890", &buf),
    );
    try testing.expectEqualStrings("see [redacted] now", sanitizeDetail("see http://10.0.0.5:11434/api/chat now", &buf));
    // Browser-parity rules: bearer/basic + credential collapse to one [redacted];
    // compound api_key/api-key heads gate the pair rule too.
    try testing.expectEqualStrings(
        "authorization [redacted] was rejected",
        sanitizeDetail("authorization Bearer abcdef1234567890 was rejected", &buf),
    );
    try testing.expectEqualStrings("Bearer abc kept", sanitizeDetail("Bearer abc kept", &buf));
    try testing.expectEqualStrings(
        "request had [redacted] attached",
        sanitizeDetail("request had api_key=supersecretvalue1 attached", &buf),
    );
    try testing.expectEqualStrings(
        "and [redacted] too",
        sanitizeDetail("and api-key:secret99 too", &buf),
    );
    try testing.expectEqualStrings(
        "Check your key and try again.",
        sanitizeDetail("Check your key and try again.", &buf),
    );
    var long: [900]u8 = undefined;
    for (&long, 0..) |*c, i| c.* = if (i % 5 == 4) ' ' else 'a';
    const cut = sanitizeDetail(&long, &buf);
    try testing.expect(cut.len <= detail_limit + "…".len);
    try testing.expect(std.mem.endsWith(u8, cut, "…"));
}

test "waitForJob: a Ctrl-C mid-call cancels and hands the job to the worker" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();

    // A job no worker ever runs: it stays .running, so the wait loop is driven purely by the
    // poll below — the same sequence a real slow provider produces.
    const job = try Job.init(a, io, "http://example.invalid", null, "{}");
    var presses: u8 = 0;
    var beats: u8 = 0;
    const waiter = Waiter{ .ctx = &presses, .poll = pressOnSecondBeat, .beat_ctx = &beats, .beat = countBeat };

    try testing.expectError(PostError.Cancelled, waitForJob(job, io, waiter));
    try testing.expectEqual(@intFromEnum(Job.State.cancelled), job.state.load(.acquire));
    try testing.expect(presses == 2); // it really waited a beat before the press landed
    try testing.expect(beats == 1); // and the spinner got that one beat, not the cancelling one
    job.discard(); // no worker exists here, so this side performs the abandoned-job cleanup
}

test "waitForJob: a reply that lands first is collected, cancel or not" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();

    const job = try Job.init(a, io, "http://example.invalid", null, "{}");
    defer job.destroy();
    job.res = .{ .status = 200, .body = try a.dupe(u8, "{\"ok\":true}") };
    _ = job.claim(.finished); // the worker got there first
    var presses: u8 = 0;

    // Even with the user pressing cancel, an answer already paid for is not thrown away.
    const res = try waitForJob(job, io, .{ .ctx = &presses, .poll = pressAlways });
    defer a.free(res.body);
    try testing.expectEqual(@as(u16, 200), res.status);
}

/// A poll that reports a Ctrl-C on its SECOND beat — one loop turn of real waiting first.
fn pressOnSecondBeat(ctx: *anyopaque, _: i32) bool {
    const n: *u8 = @ptrCast(ctx);
    n.* += 1;
    return n.* >= 2;
}

fn countBeat(ctx: *anyopaque, _: i64) void {
    const n: *u8 = @ptrCast(ctx);
    n.* += 1;
}

fn pressAlways(ctx: *anyopaque, _: i32) bool {
    const n: *u8 = @ptrCast(ctx);
    n.* += 1;
    return true;
}

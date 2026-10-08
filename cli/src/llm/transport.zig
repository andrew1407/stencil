//! Transport for the console LLM assistant: the guarded net.request path, the headers each
//! wire sends, the watched call (net/jobCall.zig, every console network call's worker), and what
//! a failed call may say (transport/detail.zig, and the §6.5 classifier in transport/anthropic.zig).
const std = @import("std");
const net = @import("../net.zig");
const jobCall = @import("../net/jobCall.zig");
const report = @import("../app/report.zig");
const wire = @import("wire.zig");
const providers = @import("providers.zig");
const anthropic = @import("transport/anthropic.zig");
const detail = @import("transport/detail.zig");

// Symbols living in the sibling llm/ modules (facade: ../llm.zig).
const memberStr = wire.memberStr;
const Request = wire.Request;

/// `LlmDisabled` = no model behind the call: the server's 503 llmDisabled, or an anthropic turn with
/// no session key or bound for plain http off loopback (§6.5) — "configure it", not "the network broke".
pub const PostError = error{ BlockedHost, HttpFailed, LlmDisabled, OutOfMemory, Cancelled, TimedOut };

/// How long one LLM call may run before it is abandoned: a vision plan over a big image is slow, but
/// a silent provider must not wedge the console.
pub const request_timeout_ms: i64 = @as(i64, providers.cli_chat_seconds) * std.time.ms_per_s;

/// What an anthropic turn with no key fails with (§6.5) — nothing has been sent.
pub const no_key_message = "no API key for this session";

/// The console's watch on a turn: its Ctrl-C poll, the spinner's beat and `request_timeout_ms`.
pub const Waiter = net.Waiter;

/// One exchange: the URL, headers and body out, the status and a capped body back.
pub const SendFn = *const fn (gpa: std.mem.Allocator, io: std.Io, url: []const u8, headers: []const std.http.Header, body: []const u8) PostError!net.Response;

pub const HeaderBuf = [4]std.http.Header;

/// The headers a request carries (§6): JSON, then the bearer — or anthropic's `x-api-key` with its
/// `anthropic-version`, and never the browser-only direct-access header.
pub fn headersFor(auth: ?[]const u8, api_key: ?[]const u8, buf: *HeaderBuf) []const std.http.Header {
    buf[0] = .{ .name = "content-type", .value = "application/json" };
    var n: usize = 1;
    if (auth) |a| {
        buf[n] = .{ .name = "authorization", .value = a };
        n += 1;
    }
    if (api_key) |k| {
        buf[n] = .{ .name = "x-api-key", .value = k };
        buf[n + 1] = .{ .name = "anthropic-version", .value = providers.get().anthropic_version };
        n += 2;
    }
    return buf[0..n];
}

/// POST a request over net.request's guarded path (SSRF checks, redirect refusal, size cap) in
/// NON-strict mode: LLM endpoints are user-named, so loopback is allowed, private ranges are not.
/// A null `waiter` runs the call to completion on this thread.
pub fn postJson(gpa: std.mem.Allocator, io: std.Io, req: *const Request, waiter: ?Waiter) PostError![]u8 {
    return postJsonVia(gpa, io, req, waiter, netSend);
}

/// `postJson` over the given exchange.
pub fn postJsonVia(gpa: std.mem.Allocator, io: std.Io, req: *const Request, waiter: ?Waiter, send: SendFn) PostError![]u8 {
    try refuseUnsafe(req);
    var hbuf: HeaderBuf = undefined;
    const headers = headersFor(req.auth, req.api_key, &hbuf);
    const w = waiter orelse return finish(gpa, req, try send(gpa, io, req.url, headers, req.body));
    // Watched: the request runs on a worker while this thread keeps reading the tty, so a
    // Ctrl-C (or the deadline) ends the wait instead of the console sitting deaf for minutes.
    return switch (jobCall.watched(io, w, sendVia, .{ send, gpa, io, req.url, headers, req.body })) {
        .done => |r| finish(gpa, req, try r),
        .cancelled => |r| {
            if (r) |res| gpa.free(res.body) else |_| {}
            // The user asked for the stop, so it is a note, not an `error:` — the deadline still is one.
            report.note("cancelled — the assistant turn was stopped\n", .{});
            return PostError.Cancelled;
        },
        .timed_out => |r| {
            if (r) |res| gpa.free(res.body) else |_| {}
            report.err("the LLM endpoint did not answer within {d}s\n", .{@divTrunc(w.timeout_ms, 1000)});
            return PostError.TimedOut;
        },
    };
}

// A runtime exchange behind the comptime-known function the watched call takes.
fn sendVia(send: SendFn, gpa: std.mem.Allocator, io: std.Io, url: []const u8, headers: []const std.http.Header, body: []const u8) PostError!net.Response {
    return send(gpa, io, url, headers, body);
}

/// §6.5's refusals before anything leaves: an anthropic turn with no session key, and a key
/// bound for plain http anywhere but loopback (a local mock or proxy the user named).
fn refuseUnsafe(req: *const Request) PostError!void {
    if (req.provider != .anthropic) return;
    if (req.api_key == null) {
        report.err(no_key_message ++ "\n", .{});
        return PostError.LlmDisabled;
    }
    if (!std.ascii.startsWithIgnoreCase(req.url, "http://")) return;
    const host = net.hostOf(req.url) orelse "";
    if (net.isLoopbackHost(host)) return;
    report.err("refusing to send the API key to '{s}' over plain http — use https\n", .{host});
    return PostError.LlmDisabled;
}

/// Turn a raw response into the reply body, reporting a non-2xx the §6.3 / §6.5 way.
fn finish(gpa: std.mem.Allocator, req: *const Request, res: net.Response) PostError![]u8 {
    if (res.status >= 200 and res.status < 300) return res.body;
    defer gpa.free(res.body);
    if (req.provider == .anthropic) {
        var mbuf: anthropic.MessageBuf = undefined;
        report.err("{s}\n", .{anthropic.message(gpa, res.status, res.body, req.api_key orelse "", &mbuf)});
        return PostError.HttpFailed;
    }
    // The reason ONCE (contract §6.3): the provider's own message when it has one —
    // the console already printed which endpoint it is asking — else the bare status.
    var buf: DetailBuf = undefined;
    const why = errorDetail(gpa, res.body, &buf);
    if (why.len != 0) {
        report.err("{s}\n", .{why});
    } else {
        report.err("the LLM endpoint answered HTTP {d}\n", .{res.status});
    }
    // Same printed message, typed: the server's llmDisabled is its own error.
    return if (isLlmDisabled(gpa, res.body)) PostError.LlmDisabled else PostError.HttpFailed;
}

/// True when a non-2xx body is the server's `{"code":"llmDisabled"}` (LLM not
/// configured) — the typed `LlmDisabled` seam, keyed on the code like every client.
pub fn isLlmDisabled(gpa: std.mem.Allocator, body: []const u8) bool {
    var parsed = std.json.parseFromSlice(std.json.Value, gpa, body, .{}) catch return false;
    defer parsed.deinit();
    const code = memberStr(parsed.value, "code") orelse return false;
    return std.mem.eql(u8, code, "llmDisabled");
}

/// The production exchange, on whichever thread calls it.
fn netSend(gpa: std.mem.Allocator, io: std.Io, url: []const u8, headers: []const std.http.Header, body: []const u8) PostError!net.Response {
    return net.request(gpa, io, url, .{
        .method = .POST,
        .payload = body,
        .extra_headers = headers,
        .timeout_ms = @intCast(request_timeout_ms), // the socket's deadline is the one the waiter watches
    });
}

pub const detail_limit = detail.detail_limit;
pub const DetailBuf = detail.DetailBuf;
pub const sanitizeDetail = detail.sanitizeDetail;
pub const errorDetail = detail.errorDetail;
pub const clip_limit = detail.clip_limit;
pub const ClipBuf = detail.ClipBuf;
pub const clip = detail.clip;
pub const anthropicError = anthropic.message;
pub const AnthropicMessageBuf = anthropic.MessageBuf;

test {
    _ = detail;
    _ = anthropic;
}

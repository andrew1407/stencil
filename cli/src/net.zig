//! URL fetch using Zig's own HTTP client (std.http.Client) — including HTTPS, via Zig's
//! built-in TLS and system CA bundle. No external tool: images, layout JSON and video clips
//! given as http(s) URLs are downloaded in-process (a clip then reaches ffmpeg as a local
//! file, media/remoteVideo.zig).
const std = @import("std");
const report = @import("app/report.zig");
const host_guard = @import("net/host.zig");
const send = @import("net/send.zig");
const job = @import("net/job.zig");
const jobCall = @import("net/jobCall.zig");
const pin = @import("net/pin.zig");

// The host/authority split + SSRF guard live in host.zig; these are the names callers use.
pub const Authority = host_guard.Authority;
pub const authorityOf = host_guard.authorityOf;
pub const hostOf = host_guard.hostOf;
pub const isLoopbackHost = host_guard.isLoopbackHost;
pub const isBlockedFetchHost = host_guard.isBlockedFetchHost;
pub const isBlockedServerHost = host_guard.isBlockedServerHost;

// The exchange itself, its options, the body cap and the per-request deadline live in send.zig.
pub const Error = send.Error;
pub const Response = send.Response;
pub const RequestOptions = send.RequestOptions;
pub const MAX_FETCH_BYTES = send.MAX_FETCH_BYTES;
pub const DEFAULT_TIMEOUT_MS = send.DEFAULT_TIMEOUT_MS;
pub const Waiter = job.Waiter;

/// What a request can fail with: the guard's and the exchange's errors, or a watch that gave up.
pub const RequestError = Error || error{ OutOfMemory, Cancelled };

pub fn isUrl(s: []const u8) bool {
    return std.ascii.startsWithIgnoreCase(s, "http://") or
        std.ascii.startsWithIgnoreCase(s, "https://");
}

/// True when `s` carries a URL scheme OTHER than http/https (`ftp://`, `file://`, `rtmp://`). These must
/// never reach ffmpeg, whose protocol surface is far wider, so the pipeline rejects them up front.
pub fn hasForeignScheme(s: []const u8) bool {
    if (isUrl(s)) return false;
    const sep = std.mem.indexOf(u8, s, "://") orelse return false;
    if (sep == 0) return false;
    // Only treat the prefix as a scheme when it is ALPHA *( ALPHA / DIGIT / "+" / "-" / "." ),
    // so a path that merely contains "://" is not misread as a foreign URL.
    for (s[0..sep], 0..) |c, i| {
        const ok = std.ascii.isAlphabetic(c) or
            (i > 0 and (std.ascii.isDigit(c) or c == '+' or c == '-' or c == '.'));
        if (!ok) return false;
    }
    return true;
}

/// SSRF guard: refuse loopback/private/link-local/metadata targets before connecting.
fn guardHost(io: std.Io, url: []const u8, strict: bool) Error!void {
    const host = hostOf(url) orelse {
        report.err("could not parse a host from URL '{s}'\n", .{url});
        return Error.BlockedHost;
    };
    if (isBlockedFetchHost(host, strict)) {
        report.err("refusing to fetch internal/blocked host '{s}'\n", .{host});
        return Error.BlockedHost;
    }
    // A DNS name must also not RESOLVE to an internal target (the literal check can't see that).
    if (!host_guard.isNumericHost(host) and host_guard.hostResolvesToBlocked(io, host, strict)) {
        report.err("refusing to fetch host '{s}' — it resolves to an internal address\n", .{host});
        return Error.BlockedHost;
    }
}

/// Refuse a collaboration server `serverTarget` (private admitted) blocks: the host as written, then
/// EVERY address it resolves to — a literal to itself, a name once. The addresses returned are the
/// ones to dial, so the address judged is the address reached.
fn guardServer(io: std.Io, host: []const u8, out: []std.Io.net.IpAddress) Error!pin.Resolved {
    if (isBlockedServerHost(host)) {
        report.err("refusing to connect to '{s}' — a link-local, metadata, multicast or reserved address is no server\n", .{host});
        return Error.BlockedHost;
    }
    const got = pin.resolve(io, host, out);
    if (got == .blocked) {
        report.err("refusing to connect to '{s}' — it resolves to a link-local, metadata, multicast or reserved address\n", .{host});
        return Error.BlockedHost;
    }
    return got;
}

/// The guard for `opts`, then the exchange under its deadline.
fn exchange(gpa: std.mem.Allocator, io: std.Io, url: []const u8, opts: RequestOptions) send.Result {
    if (!opts.server_target) {
        try guardHost(io, url, opts.strict);
        return send.deadlined(gpa, io, url, opts, &.{});
    }
    const host = hostOf(url) orelse {
        report.err("could not parse a host from URL '{s}'\n", .{url});
        return Error.BlockedHost;
    };
    var pins: [pin.max_pins]std.Io.net.IpAddress = undefined;
    return switch (try guardServer(io, host, &pins)) {
        .ok => |judged| send.deadlined(gpa, io, url, opts, judged),
        .blocked => unreachable,
        .unresolved => |e| {
            // a watch giving up mid-lookup: the waiting side says why
            if (e != error.Canceled) report.err("HTTP request failed for {s}: {s}\n", .{ url, @errorName(e) });
            return Error.HttpFailed;
        },
    };
}

/// A plain TCP stream to a collaboration server's `port` (the events feed), its host judged as every
/// exchange with that server is and dialled at an address judged. Says nothing when the host
/// does not resolve or no address answers: the feed is best-effort.
pub fn dialServer(io: std.Io, host: []const u8, port: u16) !std.Io.net.Stream {
    var pins: [pin.max_pins]std.Io.net.IpAddress = undefined;
    return switch (try guardServer(io, host, &pins)) {
        .ok => |judged| pin.connect(io, judged, port),
        .blocked => unreachable,
        .unresolved => |e| e,
    };
}

/// Send one HTTP request through the full SSRF guard — literal host check, DNS-resolution check,
/// redirect refusal — returning status + owned body capped at `MAX_FETCH_BYTES`, and giving up after
/// `opts.timeout_ms`. Every fetch uses it; on a thread with an installed watch (the console's input
/// loop) it runs on a worker a Ctrl-C ends.
pub fn request(gpa: std.mem.Allocator, io: std.Io, url: []const u8, opts: RequestOptions) RequestError!Response {
    return requestWatched(gpa, io, url, opts, null);
}

/// `request` with the guard and the exchange on a worker `waiter` — else the thread's watch — keeps
/// an eye on (net/jobCall.zig); `Cancelled`: the watch gave up first.
pub fn requestWatched(gpa: std.mem.Allocator, io: std.Io, url: []const u8, opts: RequestOptions, waiter: ?Waiter) RequestError!Response {
    return switch (jobCall.call(io, waiter, exchange, .{ gpa, io, url, opts })) {
        .done => |r| r,
        .cancelled, .timed_out => |r| {
            if (r) |res| gpa.free(res.body) else |_| {}
            return error.Cancelled;
        },
    };
}

/// GET `url`, returning the owned response body (capped at `MAX_FETCH_BYTES`). `strict` also blocks
/// loopback — pass it for sub-resources harvested from scanned content, false for a user-named URL.
pub fn fetch(gpa: std.mem.Allocator, io: std.Io, url: []const u8, strict: bool) ![]u8 {
    const res = request(gpa, io, url, .{ .strict = strict }) catch |e| {
        if (e == error.Cancelled) report.note("cancelled — {s} was not fetched\n", .{url});
        return e;
    };
    if (res.status < 200 or res.status >= 300) {
        defer gpa.free(res.body);
        report.err("HTTP {d} fetching {s}\n", .{ res.status, url });
        return Error.HttpFailed;
    }
    return res.body;
}

const testing = std.testing;

test "isUrl" {
    try testing.expect(isUrl("https://example.com/a.png"));
    try testing.expect(isUrl("HTTP://x"));
    try testing.expect(!isUrl("/local/path.png"));
}

test "hasForeignScheme" {
    // Foreign schemes are rejected …
    try testing.expect(hasForeignScheme("ftp://host/clip.mp4"));
    try testing.expect(hasForeignScheme("file:///etc/passwd.mp4"));
    try testing.expect(hasForeignScheme("rtmp://host/live"));
    // … while http(s) URLs and bare local paths are not.
    try testing.expect(!hasForeignScheme("https://example.com/v.mp4"));
    try testing.expect(!hasForeignScheme("http://h/v.webm?token=1"));
    try testing.expect(!hasForeignScheme("/home/me/clip.mp4"));
    try testing.expect(!hasForeignScheme("clip.mp4"));
    try testing.expect(!hasForeignScheme("a/b://c")); // not a scheme prefix
}

test {
    _ = job;
    _ = jobCall;
    _ = pin;
    _ = host_guard;
    _ = @import("net/addr.zig");
    _ = @import("net/ranges.zig");
}

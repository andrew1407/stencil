//! URL fetch using Zig's own HTTP client (std.http.Client) — including HTTPS, via Zig's
//! built-in TLS and system CA bundle. No external tool: images and layout JSON given as
//! http(s) URLs are downloaded in-process. (Video URLs are handled by ffmpeg, which reads
//! URLs directly; pure-Zig video decoding isn't practical — see video.zig.)
const std = @import("std");
const logo = @import("logo.zig");
const host_guard = @import("host.zig");

// The host/authority split + SSRF guard live in host.zig; these are the names callers use.
pub const Authority = host_guard.Authority;
pub const authorityOf = host_guard.authorityOf;
pub const hostOf = host_guard.hostOf;
pub const isLoopbackHost = host_guard.isLoopbackHost;
pub const isBlockedFetchHost = host_guard.isBlockedFetchHost;

pub const Error = error{ HttpFailed, BlockedHost };

/// Hard cap on the bytes read from a single fetch. Bounds memory against a hostile host that
/// streams an endless/huge body — important for scrape, which fetches many URLs harvested
/// from untrusted page content into one arena. The scratch is page-allocated (lazily
/// committed), so a small response still costs only its own size in RSS.
pub const MAX_FETCH_BYTES = 64 << 20; // 64 MiB

pub fn isUrl(s: []const u8) bool {
    return std.ascii.startsWithIgnoreCase(s, "http://") or
        std.ascii.startsWithIgnoreCase(s, "https://");
}

/// True when `s` carries a URL scheme (`scheme://…`) OTHER than http/https — e.g.
/// `ftp://`, `file://`, `rtmp://`. These must never reach ffmpeg (whose protocol surface is
/// far wider than this in-process http(s) client), so the pipeline rejects them up front.
/// A bare local path (no scheme) or an http(s) URL returns false.
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
};

pub const Response = struct {
    status: u16,
    body: []u8, // owned by the caller (present for non-2xx statuses too)
};

/// SSRF guard: refuse loopback/private/link-local/metadata targets before connecting.
fn guardHost(io: std.Io, url: []const u8, strict: bool) Error!void {
    const host = hostOf(url) orelse {
        logo.err("could not parse a host from URL '{s}'\n", .{url});
        return Error.BlockedHost;
    };
    if (isBlockedFetchHost(host, strict)) {
        logo.err("refusing to fetch internal/blocked host '{s}'\n", .{host});
        return Error.BlockedHost;
    }
    // A DNS name must also not RESOLVE to an internal target (the literal check can't see that).
    if (!host_guard.isNumericHost(host) and host_guard.hostResolvesToBlocked(io, host, strict)) {
        logo.err("refusing to fetch host '{s}' — it resolves to an internal address\n", .{host});
        return Error.BlockedHost;
    }
}

/// Send one HTTP request through the full SSRF guard and return the status + owned body
/// (capped at `MAX_FETCH_BYTES`; the caller judges non-2xx). Every outbound http(s) request
/// the CLI makes to a non-server host goes through here so the guard is uniform: the literal
/// host check, the DNS-resolution check (a hostname must not resolve to an internal
/// address), and the redirect refusal. Failures print a human-readable reason.
pub fn request(gpa: std.mem.Allocator, io: std.Io, url: []const u8, opts: RequestOptions) (Error || error{OutOfMemory})!Response {
    if (!opts.allow_named_host) try guardHost(io, url, opts.strict);

    var client: std.http.Client = .{ .allocator = gpa, .io = io };
    defer client.deinit();

    // Bounded scratch: a fixed writer returns error.WriteFailed once the body exceeds the
    // cap, aborting the stream instead of growing memory without limit. Page-allocated so a
    // small response only commits its own pages, and freed regardless of the caller's arena.
    const scratch = std.heap.page_allocator.alloc(u8, MAX_FETCH_BYTES) catch return Error.HttpFailed;
    defer std.heap.page_allocator.free(scratch);
    var body: std.Io.Writer = .fixed(scratch);

    const result = client.fetch(.{
        .location = .{ .url = url },
        .method = opts.method,
        .payload = opts.payload,
        .extra_headers = opts.extra_headers,
        .response_writer = &body,
        // Refuse redirects: a public first hop must not 30x-bounce to an internal
        // host, which would slip past the pre-fetch host check above.
        .redirect_behavior = .not_allowed,
    }) catch |e| {
        if (e == error.WriteFailed) {
            logo.err("response from {s} exceeds the {d}-byte fetch cap\n", .{ url, MAX_FETCH_BYTES });
        } else {
            logo.err("HTTP request failed for {s}: {s}\n", .{ url, @errorName(e) });
        }
        return Error.HttpFailed;
    };

    return .{
        .status = @intFromEnum(result.status),
        .body = try gpa.dupe(u8, body.buffered()),
    };
}

/// GET `url`, returning the owned response body bytes (capped at `MAX_FETCH_BYTES`).
/// `strict` blocks loopback in addition to the always-blocked internal ranges — pass it for
/// sub-resource URLs harvested from untrusted scanned content, false for a URL the user named.
pub fn fetch(gpa: std.mem.Allocator, io: std.Io, url: []const u8, strict: bool) ![]u8 {
    const res = try request(gpa, io, url, .{ .strict = strict });
    if (res.status < 200 or res.status >= 300) {
        defer gpa.free(res.body);
        logo.err("HTTP {d} fetching {s}\n", .{ res.status, url });
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

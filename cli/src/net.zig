//! URL fetch using Zig's own HTTP client (std.http.Client) — including HTTPS, via Zig's
//! built-in TLS and system CA bundle. No external tool: images and layout JSON given as
//! http(s) URLs are downloaded in-process. (Video URLs are handled by ffmpeg, which reads
//! URLs directly; pure-Zig video decoding isn't practical — see video.zig.)
const std = @import("std");
const report = @import("app/report.zig");
const host_guard = @import("net/host.zig");
const send = @import("net/send.zig");

// The host/authority split + SSRF guard live in host.zig; these are the names callers use.
pub const Authority = host_guard.Authority;
pub const authorityOf = host_guard.authorityOf;
pub const hostOf = host_guard.hostOf;
pub const isLoopbackHost = host_guard.isLoopbackHost;
pub const isBlockedFetchHost = host_guard.isBlockedFetchHost;

// The exchange itself, its options, the body cap and the per-request deadline live in send.zig.
pub const Error = send.Error;
pub const Response = send.Response;
pub const RequestOptions = send.RequestOptions;
pub const MAX_FETCH_BYTES = send.MAX_FETCH_BYTES;
pub const DEFAULT_TIMEOUT_MS = send.DEFAULT_TIMEOUT_MS;

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

/// Send one HTTP request through the full SSRF guard — literal host check, DNS-resolution check,
/// redirect refusal — returning status + owned body capped at `MAX_FETCH_BYTES`, and giving up after
/// `opts.timeout_ms`. Every fetch uses it.
pub fn request(gpa: std.mem.Allocator, io: std.Io, url: []const u8, opts: RequestOptions) send.Result {
    if (!opts.allow_named_host) try guardHost(io, url, opts.strict);
    return send.deadlined(gpa, io, url, opts);
}

/// GET `url`, returning the owned response body (capped at `MAX_FETCH_BYTES`). `strict` also blocks
/// loopback — pass it for sub-resources harvested from scanned content, false for a user-named URL.
pub fn fetch(gpa: std.mem.Allocator, io: std.Io, url: []const u8, strict: bool) ![]u8 {
    const res = try request(gpa, io, url, .{ .strict = strict });
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

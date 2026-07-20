//! URL fetch using Zig's own HTTP client (std.http.Client) — including HTTPS, via Zig's
//! built-in TLS and system CA bundle. No external tool: images and layout JSON given as
//! http(s) URLs are downloaded in-process. (Video URLs are handled by ffmpeg, which reads
//! URLs directly; pure-Zig video decoding isn't practical — see video.zig.)
const std = @import("std");

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

/// Extract the bare host (no userinfo, no port, IPv6 brackets stripped) from a URL.
/// Returns null when there is no authority to parse.
pub fn hostOf(url: []const u8) ?[]const u8 {
    var authority = if (std.mem.indexOf(u8, url, "://")) |i| url[i + 3 ..] else url;
    // The authority ends at the first path/query/fragment delimiter.
    if (std.mem.indexOfAny(u8, authority, "/?#")) |i| authority = authority[0..i];
    // Drop any userinfo ("user:pass@").
    if (std.mem.lastIndexOfScalar(u8, authority, '@')) |i| authority = authority[i + 1 ..];
    if (authority.len == 0) return null;
    // Bracketed IPv6 literal: return what's inside the brackets.
    if (authority[0] == '[') {
        const end = std.mem.indexOfScalar(u8, authority, ']') orelse return null;
        return authority[1..end];
    }
    // Otherwise strip a trailing ":port" (a single colon; multi-colon IPv6 requires brackets).
    if (std.mem.lastIndexOfScalar(u8, authority, ':')) |i| return authority[0..i];
    return authority;
}

/// True when `host` names a private / link-local / cloud-metadata / reserved target
/// that a fetch of untrusted image/layout URLs must never reach (SSRF guard).
///
/// Blocks IP-literal targets — `169.254.169.254` (cloud metadata), link-local,
/// `10.x`/`172.16-31`/`192.168` (RFC1918), CGNAT, ULA, reserved — including the
/// alternate numeric encodings (decimal/hex/octal/short-dotted) a resolver would accept.
/// When `strict` is false, loopback (`127.0.0.0/8`, `::1`, `localhost`) is deliberately
/// ALLOWED: for a URL the user named directly, the CLI is a local tool that legitimately
/// fetches from the user's own dev/fixture server on localhost (the project's own e2e does
/// exactly this), and loopback is the CLI process's own trust domain, not a network pivot.
/// When `strict` is true, loopback is ALSO blocked — used for sub-resource URLs harvested
/// from untrusted scanned page content, which must never reach a host it discovered in that
/// content, loopback services included. This is the pure/literal check; `fetch` additionally
/// resolves DNS names and
/// blocks those pointing at an internal address (`hostResolvesToBlocked`). The server-connect
/// path is intentionally exempt (users name their own servers).
pub fn isBlockedFetchHost(host: []const u8, strict: bool) bool {
    if (host.len == 0) return true;
    // IP literal (dotted-quad / IPv6)? Classify it.
    if (std.Io.net.IpAddress.parse(host, 0)) |addr| {
        return switch (addr) {
            .ip4 => |v4| isBlockedV4(v4.bytes, strict),
            .ip6 => |v6| isBlockedV6(v6.bytes, strict),
        };
    } else |_| {}
    // Alternate numeric IPv4 encodings that IpAddress.parse rejects but a libc/getaddrinfo
    // resolver would accept — a plain decimal (`2852039166`), hex (`0xA9FEA9FE`), octal, or
    // short-dotted (`10.0`, `0x7f.1`) form of an internal address. Canonicalize + classify
    // so these can't smuggle 169.254.169.254 et al. past the guard.
    if (parseInetAtonV4(host)) |v4| return isBlockedV4(v4, strict);
    // The literal `localhost` name (strict only — a name, so IpAddress.parse missed it).
    if (strict and std.ascii.eqlIgnoreCase(host, "localhost")) return true;
    // A real hostname: the DNS resolution check in fetch() covers name→internal.
    return false;
}

/// True when `host` is any IP form (literal or an alternate numeric encoding) rather than a
/// DNS name — used to skip the resolution check for something `isBlockedFetchHost` already
/// classified directly.
fn isNumericHost(host: []const u8) bool {
    if (std.Io.net.IpAddress.parse(host, 0)) |_| return true else |_| {}
    return parseInetAtonV4(host) != null;
}

/// Parse one `inet_aton`-style component: `0x`-hex, leading-`0` octal, else decimal.
fn parseAtonPart(s: []const u8) ?u64 {
    if (s.len == 0) return null;
    if (s.len >= 2 and s[0] == '0' and (s[1] == 'x' or s[1] == 'X'))
        return std.fmt.parseInt(u64, s[2..], 16) catch null;
    if (s.len >= 2 and s[0] == '0')
        return std.fmt.parseInt(u64, s[1..], 8) catch null;
    return std.fmt.parseInt(u64, s, 10) catch null;
}

/// Emulate `inet_aton` for 1–4 numeric parts (each decimal/hex/octal), returning the packed
/// IPv4 bytes, or null when `host` isn't a numeric IPv4 form. Covers the encodings resolvers
/// accept but `IpAddress.parse` (dotted-decimal only) rejects.
fn parseInetAtonV4(host: []const u8) ?[4]u8 {
    if (host.len == 0 or !std.ascii.isDigit(host[0])) return null; // must start with a digit
    var parts: [4]u64 = undefined;
    var n: usize = 0;
    var it = std.mem.splitScalar(u8, host, '.');
    while (it.next()) |part| {
        if (n >= 4) return null; // >4 parts → not an IPv4 numeric form
        parts[n] = parseAtonPart(part) orelse return null;
        n += 1;
    }
    // inet_aton: the LAST part fills the remaining low bytes; earlier parts are single octets.
    var value: u64 = 0;
    switch (n) {
        1 => value = parts[0],
        2 => {
            if (parts[0] > 0xff or parts[1] > 0xff_ffff) return null;
            value = (parts[0] << 24) | parts[1];
        },
        3 => {
            if (parts[0] > 0xff or parts[1] > 0xff or parts[2] > 0xffff) return null;
            value = (parts[0] << 24) | (parts[1] << 16) | parts[2];
        },
        4 => {
            for (parts[0..4]) |p| if (p > 0xff) return null;
            value = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
        },
        else => return null,
    }
    if (value > 0xffff_ffff) return null;
    return [4]u8{
        @intCast((value >> 24) & 0xff), @intCast((value >> 16) & 0xff),
        @intCast((value >> 8) & 0xff),  @intCast(value & 0xff),
    };
}

/// Resolve `host` via DNS and return true if ANY resolved address is an internal/blocked
/// target. This closes the "hostname with an internal A/AAAA record" SSRF vector that the
/// literal check can't see. (A residual remains: an attacker who flips the record between
/// this lookup and the client's own connect — active DNS rebinding — since std.http.Client
/// re-resolves the URL itself; the redirect refusal below still blocks the 30x variant.)
fn hostResolvesToBlocked(io: std.Io, host: []const u8, strict: bool) bool {
    const hn = std.Io.net.HostName.init(host) catch return false;
    var buf: [32]std.Io.net.HostName.LookupResult = undefined;
    var q: std.Io.Queue(std.Io.net.HostName.LookupResult) = .init(&buf);
    // lookup fills the queue (won't block at cap ≥ 16) and closes it before returning; on
    // failure we let the real fetch surface the connection error rather than block the URL.
    hn.lookup(io, &q, .{ .port = 0 }) catch return false;
    while (q.getOne(io)) |res| switch (res) {
        .address => |addr| switch (addr) {
            .ip4 => |v4| if (isBlockedV4(v4.bytes, strict)) return true,
            .ip6 => |v6| if (isBlockedV6(v6.bytes, strict)) return true,
        },
        .canonical_name => {},
    } else |_| {}
    return false;
}

fn isBlockedV4(b: [4]u8, strict: bool) bool {
    if (b[0] == 0) return true; // 0.0.0.0/8 this-network
    if (b[0] == 10) return true; // 10.0.0.0/8 private
    if (b[0] == 100 and b[1] >= 64 and b[1] <= 127) return true; // 100.64.0.0/10 CGNAT
    // 127.0.0.0/8 loopback: allowed for user-named URLs, blocked for scanned-content fetches.
    if (strict and b[0] == 127) return true;
    if (b[0] == 169 and b[1] == 254) return true; // 169.254.0.0/16 link-local (metadata)
    if (b[0] == 172 and b[1] >= 16 and b[1] <= 31) return true; // 172.16.0.0/12 private
    if (b[0] == 192 and b[1] == 168) return true; // 192.168.0.0/16 private
    if (b[0] == 192 and b[1] == 0 and (b[2] == 0 or b[2] == 2)) return true; // 192.0.0.0/24, TEST-NET-1
    if (b[0] == 198 and (b[1] == 18 or b[1] == 19)) return true; // 198.18.0.0/15 benchmarking
    if (b[0] == 198 and b[1] == 51 and b[2] == 100) return true; // TEST-NET-2
    if (b[0] == 203 and b[1] == 0 and b[2] == 113) return true; // TEST-NET-3
    if (b[0] >= 240) return true; // 240.0.0.0/4 reserved + 255.255.255.255 broadcast
    return false;
}

fn isBlockedV6(b: [16]u8, strict: bool) bool {
    // IPv4-mapped ::ffff:0:0/96 — classify the embedded IPv4.
    const mapped = [_]u8{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff };
    if (std.mem.eql(u8, b[0..12], &mapped)) return isBlockedV4(b[12..16].*, strict);
    // :: unspecified is blocked; ::1 loopback is allowed unless strict (see isBlockedV4).
    if (strict and std.mem.eql(u8, &b, &[_]u8{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 }))
        return true; // ::1 loopback, blocked for scanned-content fetches
    for (b) |x| {
        if (x != 0) break;
    } else return true; // all-zero == :: unspecified
    if (b[0] == 0xfe and (b[1] & 0xc0) == 0x80) return true; // fe80::/10 link-local
    if (b[0] == 0xfe and (b[1] & 0xc0) == 0xc0) return true; // fec0::/10 site-local (deprecated)
    if ((b[0] & 0xfe) == 0xfc) return true; // fc00::/7 unique-local
    return false;
}

/// GET `url`, returning the owned response body bytes (capped at `MAX_FETCH_BYTES`).
/// `strict` blocks loopback in addition to the always-blocked internal ranges — pass it for
/// sub-resource URLs harvested from untrusted scanned content, false for a URL the user named.
pub fn fetch(gpa: std.mem.Allocator, io: std.Io, url: []const u8, strict: bool) ![]u8 {
    // SSRF guard: refuse loopback/private/link-local/metadata targets before connecting.
    const host = hostOf(url) orelse {
        std.debug.print("error: could not parse a host from URL '{s}'\n", .{url});
        return Error.BlockedHost;
    };
    if (isBlockedFetchHost(host, strict)) {
        std.debug.print("error: refusing to fetch internal/blocked host '{s}'\n", .{host});
        return Error.BlockedHost;
    }
    // For a DNS name, also refuse when it RESOLVES to an internal target (closes the
    // hostname-with-internal-record vector the literal check above can't see).
    if (!isNumericHost(host) and hostResolvesToBlocked(io, host, strict)) {
        std.debug.print("error: refusing to fetch host '{s}' — it resolves to an internal address\n", .{host});
        return Error.BlockedHost;
    }

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
        .response_writer = &body,
        // Refuse redirects: a public first hop must not 30x-bounce to an internal
        // host, which would slip past the pre-fetch host check above.
        .redirect_behavior = .not_allowed,
    }) catch |e| {
        if (e == error.WriteFailed) {
            std.debug.print("error: response from {s} exceeds the {d}-byte fetch cap\n", .{ url, MAX_FETCH_BYTES });
        } else {
            std.debug.print("error: HTTP request failed for {s}: {s}\n", .{ url, @errorName(e) });
        }
        return Error.HttpFailed;
    };

    const code = @intFromEnum(result.status);
    if (code < 200 or code >= 300) {
        std.debug.print("error: HTTP {d} fetching {s}\n", .{ code, url });
        return Error.HttpFailed;
    }
    return gpa.dupe(u8, body.buffered());
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

test "hostOf extracts the bare host" {
    try testing.expectEqualStrings("example.com", hostOf("https://example.com/a.png").?);
    try testing.expectEqualStrings("example.com", hostOf("http://user:pass@example.com:8080/x").?);
    try testing.expectEqualStrings("169.254.169.254", hostOf("http://169.254.169.254/latest/meta-data/").?);
    try testing.expectEqualStrings("::1", hostOf("http://[::1]:9000/x").?);
    try testing.expect(hostOf("http:///only-path") == null);
}

test "isBlockedFetchHost blocks internal targets" {
    // Cloud metadata + private + CGNAT + link-local + ULA + reserved are blocked (both modes).
    try testing.expect(isBlockedFetchHost("169.254.169.254", false)); // AWS/GCP metadata
    try testing.expect(isBlockedFetchHost("10.0.0.5", false));
    try testing.expect(isBlockedFetchHost("172.16.4.4", false));
    try testing.expect(isBlockedFetchHost("172.31.255.255", false));
    try testing.expect(isBlockedFetchHost("192.168.1.1", false));
    try testing.expect(isBlockedFetchHost("100.64.0.1", false)); // CGNAT
    try testing.expect(isBlockedFetchHost("0.0.0.0", false)); // unspecified
    try testing.expect(isBlockedFetchHost("fe80::1", false)); // link-local
    try testing.expect(isBlockedFetchHost("fc00::1", false)); // ULA
    try testing.expect(isBlockedFetchHost("::", false)); // IPv6 unspecified
    try testing.expect(isBlockedFetchHost("::ffff:169.254.169.254", false)); // IPv4-mapped metadata
    try testing.expect(isBlockedFetchHost("::ffff:10.0.0.1", false)); // IPv4-mapped private

    // Loopback is ALLOWED for user-named URLs (non-strict) — local dev/fixture server.
    try testing.expect(!isBlockedFetchHost("127.0.0.1", false));
    try testing.expect(!isBlockedFetchHost("127.9.9.9", false));
    try testing.expect(!isBlockedFetchHost("localhost", false));
    try testing.expect(!isBlockedFetchHost("::1", false)); // IPv6 loopback
    try testing.expect(!isBlockedFetchHost("::ffff:127.0.0.1", false)); // IPv4-mapped loopback

    // Normal public hosts and IPs are allowed.
    try testing.expect(!isBlockedFetchHost("example.com", false));
    try testing.expect(!isBlockedFetchHost("cdn.example.org", false));
    try testing.expect(!isBlockedFetchHost("8.8.8.8", false));
    try testing.expect(!isBlockedFetchHost("93.184.216.34", false));
    try testing.expect(!isBlockedFetchHost("2606:2800:220:1:248:1893:25c8:1946", false)); // public v6
}

test "isBlockedFetchHost strict mode also blocks loopback (scanned-content fetches)" {
    // Sub-resource URLs pulled from untrusted page content must not reach loopback either.
    try testing.expect(isBlockedFetchHost("127.0.0.1", true));
    try testing.expect(isBlockedFetchHost("127.9.9.9", true));
    try testing.expect(isBlockedFetchHost("localhost", true));
    try testing.expect(isBlockedFetchHost("::1", true)); // IPv6 loopback
    try testing.expect(isBlockedFetchHost("::ffff:127.0.0.1", true)); // IPv4-mapped loopback
    try testing.expect(isBlockedFetchHost("2130706433", true)); // 127.0.0.1 decimal
    try testing.expect(isBlockedFetchHost("0x7f000001", true)); // 127.0.0.1 hex
    // Everything the non-strict mode blocks stays blocked …
    try testing.expect(isBlockedFetchHost("169.254.169.254", true));
    try testing.expect(isBlockedFetchHost("10.0.0.5", true));
    // … and public hosts stay allowed.
    try testing.expect(!isBlockedFetchHost("example.com", true));
    try testing.expect(!isBlockedFetchHost("8.8.8.8", true));
}

test "isBlockedFetchHost blocks alternate numeric IPv4 encodings" {
    // 169.254.169.254 (cloud metadata) in decimal / hex.
    try testing.expect(isBlockedFetchHost("2852039166", false));   // decimal
    try testing.expect(isBlockedFetchHost("0xA9FEA9FE", false));   // hex
    // 10.0.0.1 in decimal / hex / short-dotted; 192.168.0.1 in octal; 10.0.0.0 short.
    try testing.expect(isBlockedFetchHost("167772161", false));    // 10.0.0.1 decimal
    try testing.expect(isBlockedFetchHost("0x0A000001", false));   // 10.0.0.1 hex
    try testing.expect(isBlockedFetchHost("10.0", false));         // 10.0.0.0 short-dotted
    try testing.expect(isBlockedFetchHost("0300.0250.0.1", false)); // 192.168.0.1 octal parts

    // Loopback is allowed in numeric forms too when non-strict (127.0.0.1 = 0x7f000001).
    try testing.expect(!isBlockedFetchHost("0x7f000001", false));
    try testing.expect(!isBlockedFetchHost("2130706433", false));

    // Public numeric forms and out-of-range / non-numeric hosts are not blocked here
    // (a genuine hostname is covered by the DNS-resolution check in fetch()).
    try testing.expect(!isBlockedFetchHost("134744072", false));       // 8.8.8.8 decimal
    try testing.expect(!isBlockedFetchHost("999999999999", false));    // > u32 → not an IPv4 form
    try testing.expect(!isBlockedFetchHost("12345.example.com", false)); // starts numeric but is a name
}

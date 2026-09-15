//! Host classification for every outbound URL: the URL-authority split each front-end
//! path needs (host / port / userinfo / IPv6 brackets) and the SSRF guard that decides
//! whether a host may be reached at all. Split out of net.zig so the fetch client holds
//! only the request logic; net re-exports the names its callers already use.
const std = @import("std");

/// A URL's authority, split once for every caller that needs a host (the fetch guard,
/// serverClient's connect/cleartext checks, the LLM console's server matcher).
pub const Authority = struct {
    /// The authority exactly as written, userinfo and port included ("[::1]:9000").
    raw: []const u8,
    /// The host alone: no userinfo, no port, IPv6 brackets stripped.
    host: []const u8,
    /// The explicit port, when the authority carries a parseable one.
    port: ?u16 = null,
};

/// Split a URL — or a bare authority, when there is no `scheme://` — into its parts.
/// Returns null when there is no authority to parse.
pub fn authorityOf(url: []const u8) ?Authority {
    var raw = if (std.mem.indexOf(u8, url, "://")) |i| url[i + 3 ..] else url;
    // The authority ends at the first path/query/fragment delimiter.
    if (std.mem.indexOfAny(u8, raw, "/?#")) |i| raw = raw[0..i];
    if (raw.len == 0) return null;
    // Drop any userinfo ("user:pass@").
    var host = if (std.mem.lastIndexOfScalar(u8, raw, '@')) |i| raw[i + 1 ..] else raw;
    if (host.len == 0) return null;
    var port: ?u16 = null;
    if (host[0] == '[') {
        // Bracketed IPv6 literal: the host is what's inside, the port what follows "]:".
        const end = std.mem.indexOfScalar(u8, host, ']') orelse return null;
        const after = host[end + 1 ..];
        host = host[1..end];
        if (after.len > 1 and after[0] == ':') port = std.fmt.parseInt(u16, after[1..], 10) catch null;
    } else if (std.mem.lastIndexOfScalar(u8, host, ':')) |i| {
        // A trailing ":port" (a single colon; multi-colon IPv6 requires brackets).
        if (i + 1 < host.len) port = std.fmt.parseInt(u16, host[i + 1 ..], 10) catch null;
        host = host[0..i];
    }
    return .{ .raw = raw, .host = host, .port = port };
}

/// Extract the bare host (no userinfo, no port, IPv6 brackets stripped) from a URL.
/// Returns null when there is no authority to parse.
pub fn hostOf(url: []const u8) ?[]const u8 {
    const a = authorityOf(url) orelse return null;
    return if (a.host.len == 0) null else a.host;
}

/// True for a loopback host — `localhost` / `*.localhost`, `127.0.0.0/8` (in any numeric
/// form) or `::1`, with or without IPv6 brackets. The ONE textual loopback classifier:
/// the fetch guard's strict mode and serverClient's cleartext check share it.
pub fn isLoopbackHost(raw: []const u8) bool {
    const host = if (raw.len >= 2 and raw[0] == '[' and raw[raw.len - 1] == ']') raw[1 .. raw.len - 1] else raw;
    if (std.ascii.eqlIgnoreCase(host, "localhost")) return true;
    if (host.len > ".localhost".len and std.ascii.eqlIgnoreCase(host[host.len - ".localhost".len ..], ".localhost")) return true;
    if (std.Io.net.IpAddress.parse(host, 0)) |addr| return switch (addr) {
        .ip4 => |v4| isLoopbackV4(v4.bytes),
        .ip6 => |v6| isLoopbackV6(v6.bytes),
    } else |_| {}
    if (parseInetAtonV4(host)) |v4| return isLoopbackV4(v4);
    return false;
}

fn isLoopbackV4(b: [4]u8) bool {
    return b[0] == 127; // 127.0.0.0/8
}

fn isLoopbackV6(b: [16]u8) bool {
    const mapped = [_]u8{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff };
    if (std.mem.eql(u8, b[0..12], &mapped)) return isLoopbackV4(b[12..16].*);
    return std.mem.eql(u8, &b, &[_]u8{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 }); // ::1
}

/// True when `host` names a private / link-local / cloud-metadata / reserved target that a
/// fetch of untrusted image/layout URLs must never reach (SSRF guard). Blocks IP literals —
/// metadata, link-local, RFC1918, CGNAT, ULA, reserved — including the alternate numeric
/// encodings (decimal/hex/octal/short-dotted) a resolver would accept.
/// `strict` ALSO blocks loopback (`127.0.0.0/8`, `::1`, `localhost`): pass it for sub-resource
/// URLs harvested from untrusted scanned content, which must never reach a host they named.
/// A URL the user typed may reach loopback — the CLI's own trust domain (dev/fixture servers).
/// This is the literal check; `request` also resolves DNS names (`hostResolvesToBlocked`). The
/// server-connect path is intentionally exempt (users name their own servers) — see
/// `RequestOptions.allow_named_host`.
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
    // The loopback NAMES (strict only — names, so IpAddress.parse missed them).
    if (strict and isLoopbackHost(host)) return true;
    // A real hostname: the DNS resolution check in fetch() covers name→internal.
    return false;
}

/// True when `host` is any IP form (literal or an alternate numeric encoding) rather than a
/// DNS name — used to skip the resolution check for something `isBlockedFetchHost` already
/// classified directly.
pub fn isNumericHost(host: []const u8) bool {
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
pub fn hostResolvesToBlocked(io: std.Io, host: []const u8, strict: bool) bool {
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
    if (strict and isLoopbackV4(b)) return true;
    if (b[0] == 169 and b[1] == 254) return true; // 169.254.0.0/16 link-local (metadata)
    if (b[0] == 172 and b[1] >= 16 and b[1] <= 31) return true; // 172.16.0.0/12 private
    if (b[0] == 192 and b[1] == 168) return true; // 192.168.0.0/16 private
    if (b[0] == 192 and b[1] == 0 and (b[2] == 0 or b[2] == 2)) return true; // 192.0.0.0/24, TEST-NET-1
    if (b[0] == 198 and (b[1] == 18 or b[1] == 19)) return true; // 198.18.0.0/15 benchmarking
    if (b[0] == 198 and b[1] == 51 and b[2] == 100) return true; // TEST-NET-2
    if (b[0] == 203 and b[1] == 0 and b[2] == 113) return true; // TEST-NET-3
    if (b[0] >= 224) return true; // 224.0.0.0/4 multicast + 240.0.0.0/4 reserved + broadcast
    return false;
}

fn isBlockedV6(b: [16]u8, strict: bool) bool {
    // IPv4-mapped ::ffff:0:0/96 — classify the embedded IPv4.
    const mapped = [_]u8{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff };
    if (std.mem.eql(u8, b[0..12], &mapped)) return isBlockedV4(b[12..16].*, strict);
    // :: unspecified is blocked; ::1 loopback is allowed unless strict (see isBlockedV4).
    if (strict and isLoopbackV6(b)) return true;
    for (b) |x| {
        if (x != 0) break;
    } else return true; // all-zero == :: unspecified
    if (b[0] == 0xfe and (b[1] & 0xc0) == 0x80) return true; // fe80::/10 link-local
    if (b[0] == 0xfe and (b[1] & 0xc0) == 0xc0) return true; // fec0::/10 site-local (deprecated)
    if ((b[0] & 0xfe) == 0xfc) return true; // fc00::/7 unique-local
    return false;
}

const testing = std.testing;

test "hostOf extracts the bare host" {
    try testing.expectEqualStrings("example.com", hostOf("https://example.com/a.png").?);
    try testing.expectEqualStrings("example.com", hostOf("http://user:pass@example.com:8080/x").?);
    try testing.expectEqualStrings("169.254.169.254", hostOf("http://169.254.169.254/latest/meta-data/").?);
    try testing.expectEqualStrings("::1", hostOf("http://[::1]:9000/x").?);
    try testing.expect(hostOf("http:///only-path") == null);
}

test "authorityOf: one split for userinfo, ports, IPv6 brackets and a bare authority" {
    const serverClient = @import("serverClient.zig"); // the other call site, kept honest below
    const Case = struct { url: []const u8, raw: []const u8, host: []const u8, port: ?u16 };
    for ([_]Case{
        .{ .url = "https://example.com/a.png", .raw = "example.com", .host = "example.com", .port = null },
        .{ .url = "http://user:pass@example.com:8080/x", .raw = "user:pass@example.com:8080", .host = "example.com", .port = 8080 },
        .{ .url = "http://[::1]:9000/x", .raw = "[::1]:9000", .host = "::1", .port = 9000 },
        .{ .url = "http://[fe80::1]", .raw = "[fe80::1]", .host = "fe80::1", .port = null },
        .{ .url = "host:8090", .raw = "host:8090", .host = "host", .port = 8090 }, // bare authority, no scheme
        .{ .url = "example.com/path?q#f", .raw = "example.com", .host = "example.com", .port = null },
        .{ .url = "http://host:notaport", .raw = "host:notaport", .host = "host", .port = null },
    }) |c| {
        const a = authorityOf(c.url).?;
        try testing.expectEqualStrings(c.raw, a.raw);
        try testing.expectEqualStrings(c.host, a.host);
        try testing.expectEqual(c.port, a.port);
        // Every call site agrees on the host: hostOf, and (via net) serverClient's splitter.
        try testing.expectEqualStrings(c.host, hostOf(c.url).?);
        const hp = serverClient.hostAndPort(c.url);
        const def: u16 = if (std.ascii.startsWithIgnoreCase(c.url, "https://")) 443 else 80;
        try testing.expectEqualStrings(c.host, hp.host);
        try testing.expectEqual(c.port orelse def, hp.port);
    }
    try testing.expect(authorityOf("http:///only-path") == null);
    try testing.expect(hostOf("http:///only-path") == null);
}

test "isLoopbackHost: names, brackets and every numeric 127/::1 form" {
    for ([_][]const u8{ "localhost", "LOCALHOST", "app.localhost", "127.0.0.1", "127.9.9.9", "::1", "[::1]", "::ffff:127.0.0.1", "2130706433", "0x7f000001" }) |h|
        try testing.expect(isLoopbackHost(h));
    for ([_][]const u8{ "example.com", "localhost.example.com", "10.0.0.1", "::2", "8.8.8.8", "" }) |h|
        try testing.expect(!isLoopbackHost(h));
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
    try testing.expect(isBlockedFetchHost("224.0.0.1", false)); // multicast
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
    try testing.expect(isBlockedFetchHost("2852039166", false)); // decimal
    try testing.expect(isBlockedFetchHost("0xA9FEA9FE", false)); // hex
    // 10.0.0.1 in decimal / hex / short-dotted; 192.168.0.1 in octal; 10.0.0.0 short.
    try testing.expect(isBlockedFetchHost("167772161", false)); // 10.0.0.1 decimal
    try testing.expect(isBlockedFetchHost("0x0A000001", false)); // 10.0.0.1 hex
    try testing.expect(isBlockedFetchHost("10.0", false)); // 10.0.0.0 short-dotted
    try testing.expect(isBlockedFetchHost("0300.0250.0.1", false)); // 192.168.0.1 octal parts

    // Loopback is allowed in numeric forms too when non-strict (127.0.0.1 = 0x7f000001).
    try testing.expect(!isBlockedFetchHost("0x7f000001", false));
    try testing.expect(!isBlockedFetchHost("2130706433", false));

    // Public numeric forms and out-of-range / non-numeric hosts are not blocked here
    // (a genuine hostname is covered by the DNS-resolution check in fetch()).
    try testing.expect(!isBlockedFetchHost("134744072", false)); // 8.8.8.8 decimal
    try testing.expect(!isBlockedFetchHost("999999999999", false)); // > u32 → not an IPv4 form
    try testing.expect(!isBlockedFetchHost("12345.example.com", false)); // starts numeric but is a name
}

//! Host classification for every outbound URL: the URL-authority split each front-end
//! path needs (host / port / userinfo / IPv6 brackets) and the SSRF guard that decides
//! whether a host may be reached at all. Split out of net.zig so the fetch client holds
//! only the request logic; net re-exports the names its callers already use.
const std = @import("std");
const addr = @import("addr.zig");
const ranges = @import("ranges.zig");

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

/// True for a loopback host — `localhost`/`*.localhost`, or any address the table files under
/// `loopback`, carried or not. The ONE loopback classifier: the fetch guard and serverClient share it.
pub fn isLoopbackHost(raw: []const u8) bool {
    const host = if (raw.len >= 2 and raw[0] == '[' and raw[raw.len - 1] == ']') raw[1 .. raw.len - 1] else raw;
    if (std.ascii.eqlIgnoreCase(host, "localhost")) return true;
    if (host.len > ".localhost".len and std.ascii.eqlIgnoreCase(host[host.len - ".localhost".len ..], ".localhost")) return true;
    const a = addr.parseHost(host) orelse return false;
    return ranges.isLoopback(a);
}

/// SSRF literal check under blockedRanges.json's `fetch` policy — every numeric spelling a resolver
/// accepts; `strict` (a URL from fetched content) refuses loopback too. DNS: `hostResolvesToBlocked`.
pub fn isBlockedFetchHost(host: []const u8, strict: bool) bool {
    if (host.len == 0) return true;
    if (addr.parseHost(host)) |a| return ranges.blocked(a, .fetch, .{ .allow_loopback = !strict });
    // The loopback NAMES (strict only); any other name is judged by what it resolves to.
    return strict and isLoopbackHost(host);
}

/// True when `host` is an address in any spelling rather than a DNS name — used to skip the
/// resolution check for what `isBlockedFetchHost` already judged.
pub fn isNumericHost(host: []const u8) bool {
    return addr.parseHost(host) != null;
}

/// A collaboration-server target the `serverTarget` policy refuses: link-local, cloud metadata,
/// unspecified, multicast, reserved — whatever the host literally names. Private ranges pass.
pub fn isBlockedServerHost(host: []const u8) bool {
    const a = addr.parseHost(host) orelse return false;
    return ranges.blocked(a, .server_target, .{ .allow_private = true });
}

/// Resolve `host` and return true if ANY resolved address is blocked — the "hostname with an internal
/// A/AAAA record" vector. Active DNS rebinding remains: std.http.Client re-resolves the URL itself.
pub fn hostResolvesToBlocked(io: std.Io, host: []const u8, strict: bool) bool {
    const hn = std.Io.net.HostName.init(host) catch return false;
    var buf: [32]std.Io.net.HostName.LookupResult = undefined;
    var q: std.Io.Queue(std.Io.net.HostName.LookupResult) = .init(&buf);
    // lookup fills the queue (won't block at cap ≥ 16) and closes it before returning; on
    // failure we let the real fetch surface the connection error rather than block the URL.
    hn.lookup(io, &q, .{ .port = 0 }) catch return false;
    while (q.getOne(io)) |res| switch (res) {
        .address => |ip| if (ranges.blocked(addr.Addr.fromIp(ip), .fetch, .{ .allow_loopback = !strict })) return true,
        .canonical_name => {},
    } else |_| {}
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
    const serverClient = @import("../server/client.zig"); // the other call site, kept honest below
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

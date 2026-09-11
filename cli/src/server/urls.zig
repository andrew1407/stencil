//! Server URLs: normalizing what the user typed into an origin, pulling an invite token
//! out of a share link, and the host/port pair the raw-TCP edit channel dials. Only a URL
//! the USER gave is ever normalized here — nothing is discovered from fetched content.
const std = @import("std");
const net = @import("../net.zig");
const testing = std.testing;

// pure helpers (no network; unit-tested)

/// Normalize a server URL to a clean origin: add http:// if no scheme, drop any
/// path/trailing slash. Caller owns the returned slice.
/// True for a loopback host, where plaintext http is safe because the bytes never leave
/// the machine. The one classifier, shared with the fetch guard's strict mode.
pub const isLoopbackHost = net.isLoopbackHost;

/// True when `base` would send the bearer token + image bytes in CLEARTEXT to a remote
/// host (http scheme and not loopback) — connect() warns on these.
pub fn isInsecureRemote(base: []const u8) bool {
    if (!std.ascii.startsWithIgnoreCase(base, "http://")) return false;
    return !isLoopbackHost(hostAndPort(base).host);
}

/// Split an invite link's `#token=<value>` fragment off a connect URL and pick the
/// effective supplied token — an explicitly-passed token wins over the fragment.
/// Both returned slices alias the inputs.
pub fn splitInviteToken(url: []const u8, token_opt: ?[]const u8) struct { url: []const u8, token: ?[]const u8 } {
    if (std.mem.indexOf(u8, url, "#token=")) |i| {
        const frag = std.mem.trim(u8, url[i + "#token=".len ..], " \t\r\n");
        const tok = token_opt orelse (if (frag.len != 0) frag else null);
        return .{ .url = url[0..i], .token = tok };
    }
    return .{ .url = url, .token = token_opt };
}

pub fn normalizeBase(gpa: std.mem.Allocator, url: []const u8) ![]u8 {
    var s = std.mem.trim(u8, url, " \t\r\n");
    var buf: []u8 = undefined;
    var owned = false;
    if (!std.ascii.startsWithIgnoreCase(s, "http://") and !std.ascii.startsWithIgnoreCase(s, "https://")) {
        // Secure by default: a bare REMOTE host gets https; loopback keeps plaintext http
        // (localhost dev servers, traffic never leaves the machine). An explicit scheme is
        // preserved, so "http://<remote>" still works — the user opts into cleartext.
        const scheme = if (isLoopbackHost(net.hostOf(s) orelse "")) "http://" else "https://";
        buf = try std.fmt.allocPrint(gpa, "{s}{s}", .{ scheme, s });
        owned = true;
        s = buf;
    }
    // Keep scheme + authority only (strip the first '/' after "scheme://").
    const scheme_end = std.mem.indexOf(u8, s, "://").? + 3;
    const rest = s[scheme_end..];
    const slash = std.mem.indexOfScalar(u8, rest, '/');
    const end = if (slash) |i| scheme_end + i else s.len;
    const result = try gpa.dupe(u8, s[0..end]);
    if (owned) gpa.free(buf);
    return result;
}

// live edit/events transport (raw TCP, NDJSON)
//
// The CLI edits a single raster image, so it does NOT push collaborative edit/save
// frames (that would clobber other clients' layouts). It only subscribes read-only to
// the global events feed (a `hello` with empty projectId) to learn when a project it is
// editing was saved elsewhere. Best-effort: socket errors silently disable live events.

/// The raw-TCP edit port pairs with the REST port: the server ships HTTP on :8090 and
/// the TCP edit channel on :8091, so the edit port is the REST port + 1.
pub fn editPort(rest_port: u16) u16 {
    return rest_port +% 1;
}

pub const HostPort = struct { host: []const u8, port: u16 };

/// Split a normalized origin ("scheme://host[:port]") into host + REST port, defaulting
/// the port by scheme (443 for https, else 80). The host slices into `base`. Pure.
pub fn hostAndPort(base: []const u8) HostPort {
    const def: u16 = if (std.ascii.startsWithIgnoreCase(base, "https://")) 443 else 80;
    const a = net.authorityOf(base) orelse return .{ .host = base, .port = def };
    return .{ .host = a.host, .port = a.port orelse def };
}

test "normalizeBase is secure by default and strips path/slash" {
    const a = testing.allocator;
    const cases = [_]struct { in: []const u8, out: []const u8 }{
        // Bare REMOTE host → https (don't leak a token over cleartext); loopback → http.
        .{ .in = "host:8090", .out = "https://host:8090" },
        .{ .in = "localhost:8090", .out = "http://localhost:8090" },
        .{ .in = "127.0.0.1:8090", .out = "http://127.0.0.1:8090" },
        // An explicit scheme is preserved (deliberate opt-in); path/slash stripped.
        .{ .in = "http://host:8090/", .out = "http://host:8090" },
        .{ .in = "  https://h:1/projects  ", .out = "https://h:1" },
        .{ .in = "http://h:2", .out = "http://h:2" },
    };
    for (cases) |c| {
        const got = try normalizeBase(a, c.in);
        defer a.free(got);
        try testing.expectEqualStrings(c.out, got);
    }
}

test "splitInviteToken: fragment parsed, explicit token wins, plain URL unchanged" {
    // Invite link: fragment stripped, value becomes the supplied token.
    const inv = splitInviteToken("http://localhost:8090#token=abc123", null);
    try testing.expectEqualStrings("http://localhost:8090", inv.url);
    try testing.expectEqualStrings("abc123", inv.token.?);
    // An explicitly-passed token wins over the fragment (fragment still stripped).
    const exp = splitInviteToken("http://localhost:8090#token=abc123", "explicit");
    try testing.expectEqualStrings("http://localhost:8090", exp.url);
    try testing.expectEqualStrings("explicit", exp.token.?);
    // Fragment-less URL passes through untouched.
    const plain = splitInviteToken("https://host:8090", null);
    try testing.expectEqualStrings("https://host:8090", plain.url);
    try testing.expect(plain.token == null);
    // An empty fragment value is stripped but supplies no token.
    const empty = splitInviteToken("http://localhost:8090#token=", null);
    try testing.expectEqualStrings("http://localhost:8090", empty.url);
    try testing.expect(empty.token == null);
}

test "isLoopbackHost and isInsecureRemote classify the connection" {
    try testing.expect(isLoopbackHost("localhost"));
    try testing.expect(isLoopbackHost("127.0.0.1"));
    try testing.expect(isLoopbackHost("::1"));
    try testing.expect(isLoopbackHost("[::1]")); // bracketed IPv6 (parity with the other front-ends)
    try testing.expect(!isLoopbackHost("example.com"));
    // Only cleartext-to-a-remote-host is insecure.
    try testing.expect(isInsecureRemote("http://example.com:8090"));
    try testing.expect(!isInsecureRemote("http://localhost:8090"));
    try testing.expect(!isInsecureRemote("http://127.0.0.1:8090"));
    try testing.expect(!isInsecureRemote("https://example.com:8090"));
}

test "editPort pairs with the REST port (+1)" {
    try testing.expectEqual(@as(u16, 8091), editPort(8090));
    try testing.expectEqual(@as(u16, 81), editPort(80));
}

test "hostAndPort splits host + port, defaulting by scheme" {
    const a = hostAndPort("http://host:8090");
    try testing.expectEqualStrings("host", a.host);
    try testing.expectEqual(@as(u16, 8090), a.port);

    const b = hostAndPort("https://example.com");
    try testing.expectEqualStrings("example.com", b.host);
    try testing.expectEqual(@as(u16, 443), b.port);

    const c = hostAndPort("http://10.0.0.1");
    try testing.expectEqualStrings("10.0.0.1", c.host);
    try testing.expectEqual(@as(u16, 80), c.port);
}

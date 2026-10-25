//! Which credential a server URL is dialled with (CONTRACT.md §1): `--token`, else an invite
//! link's own `#token=`, else the `STENCIL_SERVER_TOKENS` entry for the URL's origin, else
//! `STENCIL_SERVER_TOKEN`. Tokens are never printed, so a malformed entry is skipped silently.
const std = @import("std");
const net = @import("../net.zig");
const urls = @import("urls.zig");

/// Where a credential came from, said beside a connection a link opened (the token itself never is).
pub const Source = enum { explicit, invite, per_origin, single, none };

/// The token `url` connects with, or null for a self-issued session. `per_origin` is a
/// comma list of `origin=token` pairs; only the first `=` splits one. Slices alias the inputs.
pub fn pick(explicit: ?[]const u8, per_origin: ?[]const u8, single: ?[]const u8, url: []const u8) ?[]const u8 {
    return choose(explicit, per_origin, single, url).token;
}

/// pick() with the source of what it picked.
pub fn choose(explicit: ?[]const u8, per_origin: ?[]const u8, single: ?[]const u8, url: []const u8) struct { token: ?[]const u8, source: Source } {
    if (explicit) |t| return .{ .token = t, .source = .explicit };
    // connect() reads the link's own token; returning null leaves it to do so.
    if (urls.splitInviteToken(url, null).token != null) return .{ .token = null, .source = .invite };
    const fallback = nonEmpty(single);
    const last: Source = if (fallback != null) .single else .none;
    const want = originOf(url) orelse return .{ .token = fallback, .source = last };
    var pairs = std.mem.splitScalar(u8, per_origin orelse "", ',');
    while (pairs.next()) |raw| {
        const pair = std.mem.trim(u8, raw, " \t\r\n");
        const eq = std.mem.indexOfScalar(u8, pair, '=') orelse continue;
        const origin = originOf(std.mem.trim(u8, pair[0..eq], " \t")) orelse continue;
        if (!origin.same(want)) continue;
        if (nonEmpty(pair[eq + 1 ..])) |t| return .{ .token = t, .source = .per_origin };
    }
    return .{ .token = fallback, .source = last };
}

fn nonEmpty(s: ?[]const u8) ?[]const u8 {
    const t = std.mem.trim(u8, s orelse return null, " \t\r\n");
    return if (t.len == 0) null else t;
}

/// scheme + host + effective port; a bare host is https unless loopback, as normalizeBase has it.
const Origin = struct {
    https: bool,
    host: []const u8,
    port: u16,

    fn same(a: Origin, b: Origin) bool {
        return a.https == b.https and a.port == b.port and std.ascii.eqlIgnoreCase(a.host, b.host);
    }
};

fn originOf(url: []const u8) ?Origin {
    const t = std.mem.trim(u8, url, " \t\r\n");
    const https = if (std.ascii.startsWithIgnoreCase(t, "https://"))
        true
    else if (std.ascii.startsWithIgnoreCase(t, "http://"))
        false
    else if (std.mem.indexOf(u8, t, "://") != null)
        return null
    else
        !net.isLoopbackHost(net.hostOf(t) orelse return null);
    const a = net.authorityOf(t) orelse return null;
    if (a.host.len == 0) return null;
    return .{ .https = https, .host = a.host, .port = a.port orelse (if (https) @as(u16, 443) else 80) };
}

const testing = std.testing;

test "pick: --token wins, then an invite link's token, then the per-origin map, then the single" {
    const map = "http://a.test:8090=tok-a, https://B.test=tok=b";
    try testing.expectEqualStrings("flag", pick("flag", map, "one", "http://a.test:8090").?);
    try testing.expect(pick(null, map, "one", "http://a.test:8090#token=link") == null);
    try testing.expectEqualStrings("tok-a", pick(null, map, "one", "http://a.test:8090/projects").?);
    try testing.expectEqualStrings("tok=b", pick(null, map, "one", "https://b.test:443").?);
    try testing.expectEqualStrings("one", pick(null, map, "one", "http://c.test").?);
    try testing.expect(pick(null, map, null, "http://c.test") == null);
    try testing.expect(pick(null, null, "  ", "http://c.test") == null);
}

test "pick: origins compare by scheme, host case and effective port" {
    const map = "localhost:8090=loop,remote.test=bare,http://x.test=plain";
    try testing.expectEqualStrings("loop", pick(null, map, null, "http://LOCALHOST:8090").?);
    try testing.expectEqualStrings("bare", pick(null, map, null, "https://remote.test:443/").?);
    try testing.expect(pick(null, map, null, "http://remote.test") == null);
    try testing.expect(pick(null, map, null, "https://x.test") == null);
    try testing.expect(pick(null, map, null, "http://x.test:8080") == null);
    // A malformed entry or an empty token is passed over, never an error.
    try testing.expectEqualStrings("ok", pick(null, "junk,ftp://x.test=no,http://y.test=,http://y.test=ok", null, "http://y.test").?);
}

test "choose: names where each pick came from" {
    const map = "http://a.test:8090=tok-a";
    try testing.expectEqual(Source.explicit, choose("flag", map, "one", "http://a.test:8090").source);
    try testing.expectEqual(Source.invite, choose(null, map, "one", "http://a.test:8090#token=link").source);
    try testing.expectEqual(Source.per_origin, choose(null, map, "one", "http://a.test:8090").source);
    try testing.expectEqual(Source.single, choose(null, map, "one", "http://c.test").source);
    try testing.expectEqual(Source.none, choose(null, map, null, "http://c.test").source);
}

//! Opening a connection: URL normalization, what the supplied credential turns out to
//! be (session / admin / none) and the session token the client then runs on. Only a URL
//! the USER named is ever dialled (see .claude/rules/security.md).
const std = @import("std");
const report = @import("../app/report.zig");
const net = @import("../net.zig");
const errors = @import("errors.zig");
const urls = @import("urls.zig");
const parse = @import("parse.zig");
const http = @import("http.zig");
const rest = @import("rest.zig");

const Error = errors.Error;
const Transport = http.Transport;
const rawRequest = http.rawRequest;
const saveReject = http.saveReject;
const restoreReject = http.restoreReject;
const lastReject = http.lastReject;
const Client = rest.Client;
const CredentialKind = rest.CredentialKind;
const splitInviteToken = urls.splitInviteToken;
const normalizeBase = urls.normalizeBase;
const isInsecureRemote = urls.isInsecureRemote;
const parseToken = parse.parseToken;

/// Connect to a server: normalize the URL, then either validate the supplied token (a rejected one is
/// retried as the ADMIN credential, minting a session) or issue a fresh one (POST /auth/token).
pub fn connect(gpa: std.mem.Allocator, io: std.Io, url: []const u8, token_opt: ?[]const u8) !Client {
    // Invite links carry the token as a `#token=` fragment; an explicit token wins.
    const invite = splitInviteToken(url, token_opt);
    const base = try normalizeBase(gpa, invite.url);
    errdefer gpa.free(base);
    // blockedRanges.json `serverTarget` with allowPrivate: a LAN server is fine, metadata never is.
    if (net.isBlockedServerHost(urls.hostAndPort(base).host)) {
        report.err("refusing to connect to {s} — a link-local, metadata, multicast or reserved address is no server\n", .{base});
        return Error.HttpFailed;
    }
    if (isInsecureRemote(base))
        report.note("connecting to {s} over plaintext http — your access token and images are sent unencrypted; use https on untrusted networks\n", .{base});

    const resolved = try resolveToken(gpa, io, base, invite.token, rawRequest);
    errdefer gpa.free(resolved.token);
    const auth = try std.fmt.allocPrint(gpa, "Bearer {s}", .{resolved.token});
    errdefer gpa.free(auth);
    const credential = try gpa.dupe(u8, invite.token orelse "");
    errdefer gpa.free(credential);
    return Client{
        .gpa = gpa,
        .io = io,
        .base = base,
        .token = resolved.token,
        .auth = auth,
        .credential = credential,
        .credential_kind = resolved.kind,
    };
}

/// A resolved session: the token the connection runs on plus what the supplied
/// credential proved to be. Caller owns `token`.
pub const Resolved = struct { token: []u8, kind: CredentialKind };

/// The session token a connection runs on: a supplied token is validated with GET /auth/session
/// (rejected → retried as an admin credential), no token issues a fresh one. `transport` is the seam.
pub fn resolveToken(
    gpa: std.mem.Allocator,
    io: std.Io,
    base: []const u8,
    token_opt: ?[]const u8,
    transport: Transport,
) !Resolved {
    if (token_opt) |t| {
        const auth = try std.fmt.allocPrint(gpa, "Bearer {s}", .{t});
        defer gpa.free(auth);
        if (probeSession(gpa, io, base, auth, transport)) |_| {
            // The server knows it as a live session: an ordinary session token, not an admin credential.
            return .{ .token = try gpa.dupe(u8, t), .kind = .session };
        } else |e| {
            if (e != Error.Unauthorized) return e;
            // Not a session token — but it may be the server's ADMIN token, so try minting with it. On failure
            // report the probe's rejection: the mint's "admin token required" would misname a plain wrong token.
            const probe_reject = saveReject();
            const body = issueToken(gpa, io, base, auth, transport) catch |e2| {
                if (e2 == Error.Unauthorized) restoreReject(probe_reject);
                return e2;
            };
            defer gpa.free(body);
            // Minting succeeded: the credential is proven admin (browser handshake parity).
            return .{ .token = try parseToken(gpa, body), .kind = .admin };
        }
    }
    const body = try issueToken(gpa, io, base, null, transport);
    defer gpa.free(body);
    return .{ .token = try parseToken(gpa, body), .kind = .none };
}

/// Whether `auth` is a live session: GET /auth/session (401 for anything else, an admin token
/// included); a server older than that route answers 404, and one project listed stands in.
fn probeSession(gpa: std.mem.Allocator, io: std.Io, base: []const u8, auth: []const u8, transport: Transport) !void {
    const headers = [_]std.http.Header{.{ .name = "authorization", .value = auth }};
    for ([_][]const u8{ "/auth/session", "/projects?limit=1" }) |path| {
        const url = try std.fmt.allocPrint(gpa, "{s}{s}", .{ base, path });
        defer gpa.free(url);
        const body = transport(gpa, io, url, .GET, null, &headers) catch |e| {
            if (e == Error.NotFound) continue;
            return e;
        };
        gpa.free(body);
        return;
    }
    return Error.NotFound;
}

/// POST /auth/token, optionally with an admin bearer, returning the response body.
pub fn issueToken(gpa: std.mem.Allocator, io: std.Io, base: []const u8, auth_opt: ?[]const u8, transport: Transport) ![]u8 {
    const url = try std.fmt.allocPrint(gpa, "{s}/auth/token", .{base});
    defer gpa.free(url);
    var headers: [2]std.http.Header = undefined;
    var n: usize = 0;
    headers[n] = .{ .name = "content-type", .value = "application/json" };
    n += 1;
    if (auth_opt) |a| {
        headers[n] = .{ .name = "authorization", .value = a };
        n += 1;
    }
    return transport(gpa, io, url, .POST, "{}", headers[0..n]);
}

/// Print why a connect failed: the server's own rejection (status + message) when the
/// last response carried one, else the bare transport error name.
pub fn printConnectError(url: []const u8, e: anyerror) void {
    if (lastReject()) |r| {
        report.err("server rejected connection ({d}): {s}\n", .{ r.status, r.message });
    } else {
        report.err("could not connect to {s} ({s})\n", .{ url, @errorName(e) });
    }
}

const testing = std.testing;

// A server that knows GET /auth/session, or (`old`) one that predates it; either way every
// URL it was asked for is kept, so a probe that lists every project shows up.
const FakeServer = struct {
    var old = false;
    var asked: [8][64]u8 = undefined;
    var asked_n: usize = 0;

    fn run(gpa: std.mem.Allocator, _: std.Io, target: []const u8, _: std.http.Method, _: ?[]const u8, headers: []const std.http.Header) errors.TransportError![]u8 {
        const url = target;
        if (asked_n < asked.len) {
            const n = @min(url.len, 63);
            @memcpy(asked[asked_n][0..n], url[0..n]);
            asked[asked_n][n] = 0;
            asked_n += 1;
        }
        var auth: []const u8 = "";
        for (headers) |h| if (std.ascii.eqlIgnoreCase(h.name, "authorization")) {
            auth = h.value;
        };
        const session = std.mem.eql(u8, auth, "Bearer SESSION");
        if (std.mem.endsWith(u8, url, "/auth/token")) {
            if (!std.mem.eql(u8, auth, "Bearer ADMIN")) return Error.Unauthorized;
            return gpa.dupe(u8, "{\"token\":\"minted\"}");
        }
        if (std.mem.endsWith(u8, url, "/auth/session")) {
            if (old) return Error.NotFound;
            return if (session) gpa.dupe(u8, "{\"sessionId\":\"s1\",\"expiresAt\":1}") else Error.Unauthorized;
        }
        if (std.mem.endsWith(u8, url, "/projects?limit=1")) return if (session) gpa.dupe(u8, "{\"projects\":[]}") else Error.Unauthorized;
        return Error.HttpFailed; // a full listing is never how a token is checked
    }

    fn askedFor(i: usize) []const u8 {
        return std.mem.sliceTo(&asked[i], 0);
    }
};

test "resolveToken checks a token with GET /auth/session, and one project on a server without it" {
    const a = testing.allocator;
    for ([_]bool{ false, true }) |old| {
        FakeServer.old = old;
        FakeServer.asked_n = 0;
        var r = try resolveToken(a, undefined, "http://s", "SESSION", FakeServer.run);
        try testing.expectEqual(CredentialKind.session, r.kind);
        a.free(r.token);
        try testing.expectEqualStrings("http://s/auth/session", FakeServer.askedFor(0));
        try testing.expectEqual(@as(usize, if (old) 2 else 1), FakeServer.asked_n);
        if (old) try testing.expectEqualStrings("http://s/projects?limit=1", FakeServer.askedFor(1));

        // An admin token is no session (401), so it is proven by the mint, as before.
        r = try resolveToken(a, undefined, "http://s", "ADMIN", FakeServer.run);
        try testing.expectEqual(CredentialKind.admin, r.kind);
        try testing.expectEqualStrings("minted", r.token);
        a.free(r.token);

        try testing.expectError(Error.Unauthorized, resolveToken(a, undefined, "http://s", "NOPE", FakeServer.run));
    }
}

test "connect refuses a link-local or metadata server before dialling it" {
    var threaded = std.Io.Threaded.init(std.testing.allocator, .{});
    defer threaded.deinit();
    for ([_][]const u8{ "http://169.254.169.254", "http://[fe80::1]:8090", "http://[::ffff:169.254.169.254]", "http://224.0.0.1" }) |url|
        try std.testing.expectError(Error.HttpFailed, connect(std.testing.allocator, threaded.io(), url, null));
}

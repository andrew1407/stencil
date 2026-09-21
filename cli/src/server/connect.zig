//! Opening a connection: URL normalization, what the supplied credential turns out to
//! be (session / admin / none) and the session token the client then runs on. Only a URL
//! the USER named is ever dialled (see .claude/rules/security.md).
const std = @import("std");
const report = @import("../app/report.zig");
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

/// The session token a connection runs on: a supplied token is validated with a GET /projects probe
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
        const probe_url = try std.fmt.allocPrint(gpa, "{s}/projects", .{base});
        defer gpa.free(probe_url);
        if (transport(gpa, io, probe_url, .GET, null, &.{.{ .name = "authorization", .value = auth }})) |body| {
            gpa.free(body);
            // It lists projects: an ordinary session token, not an admin credential.
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

//! The collaboration server's client: one connection, its credential, and the REST calls
//! the console and the flag mode make. The wire details live in server/ — URL normalization,
//! the layout payload, response parsing, the live edit channel and the `/projects` time
//! spans. Only a URL the USER named is ever dialled (see .claude/rules/security.md).
const std = @import("std");
const report = @import("report.zig");
const net = @import("net.zig");
const sanitize = @import("sanitize.zig");
const errors = @import("server/errors.zig");
const urls = @import("server/urls.zig");
const layout_payload = @import("server/payload.zig");
const parse = @import("server/parse.zig");
const editchan = @import("server/edit.zig");
const timespan = @import("server/format.zig");
const http = @import("server/http.zig");
const rest = @import("server/rest.zig");
const meta = @import("server/meta.zig");
const connection = @import("server/connect.zig");

pub const Error = errors.Error;
pub const TransportError = errors.TransportError;
pub const isLoopbackHost = urls.isLoopbackHost;
pub const isInsecureRemote = urls.isInsecureRemote;
pub const splitInviteToken = urls.splitInviteToken;
pub const normalizeBase = urls.normalizeBase;
pub const editPort = urls.editPort;
pub const HostPort = urls.HostPort;
pub const hostAndPort = urls.hostAndPort;
pub const CropRect = layout_payload.CropRect;
pub const PageMeta = layout_payload.PageMeta;
pub const buildLayout = layout_payload.buildLayout;
pub const ProjectRef = parse.ProjectRef;
pub const ProjectInfo = parse.ProjectInfo;
pub const freeStrList = parse.freeStrList;
pub const freeProjectList = parse.freeProjectList;
pub const Event = editchan.Event;
pub const parseEvent = editchan.parseEvent;
pub const EditConn = editchan.EditConn;
pub const frame = editchan.frame;
pub const helloFrame = editchan.helloFrame;
pub const formatAgo = timespan.formatAgo;
pub const formatUntil = timespan.formatUntil;

// Names the Client bodies below use unqualified, so the methods read as they always did.
const parseToken = parse.parseToken;
const parseProjectId = parse.parseProjectId;
const findIdByName = parse.findIdByName;
const findProjectByName = parse.findProjectByName;
const parseProjectVersion = parse.parseProjectVersion;
const parseProjectList = parse.parseProjectList;
const parseProjectKeywords = parse.parseProjectKeywords;
const parseProjectStringField = parse.parseProjectStringField;
const parseErrorMessage = parse.parseErrorMessage;
const dupeStrList = parse.dupeStrList;
const jsonBody = layout_payload.body;
pub const Transport = http.Transport;
pub const rawRequest = http.rawRequest;
pub const Reject = http.Reject;
pub const lastReject = http.lastReject;
pub const CredentialKind = rest.CredentialKind;
pub const Client = rest.Client;
pub const connect = connection.connect;
pub const printConnectError = connection.printConnectError;

const resolveToken = connection.resolveToken;
const recordReject = http.recordReject;
const saveReject = http.saveReject;
const restoreReject = http.restoreReject;
const testing = std.testing;

const FakeRemint = struct {
    var calls: usize = 0;
    var mint_ok: bool = true;

    fn headerValue(headers: []const std.http.Header, name: []const u8) []const u8 {
        for (headers) |h| if (std.ascii.eqlIgnoreCase(h.name, name)) return h.value;
        return "";
    }

    fn run(
        gpa: std.mem.Allocator,
        io: std.Io,
        url: []const u8,
        method: std.http.Method,
        payload: ?[]const u8,
        headers: []const std.http.Header,
    ) TransportError![]u8 {
        _ = io;
        _ = payload;
        calls += 1;
        const auth = headerValue(headers, "authorization");
        if (std.mem.endsWith(u8, url, "/auth/token")) {
            // The mint carries the stored credential as bearer.
            if (method != .POST or !mint_ok or !std.mem.eql(u8, auth, "Bearer ADMIN"))
                return Error.Unauthorized;
            return gpa.dupe(u8, "{\"token\":\"fresh\",\"expiresAt\":0}");
        }
        // /projects: the stale session is rejected, the re-minted one accepted.
        if (std.mem.eql(u8, auth, "Bearer fresh")) return gpa.dupe(u8, "{\"projects\":[]}");
        return Error.Unauthorized;
    }
};

fn remintTestClient(credential: []const u8) !Client {
    const a = testing.allocator;
    return Client{
        .gpa = a,
        .io = undefined, // the fake transport never touches it
        .base = try a.dupe(u8, "http://s"),
        .token = try a.dupe(u8, "stale"),
        .auth = try a.dupe(u8, "Bearer stale"),
        .credential = try a.dupe(u8, credential),
        .transport = FakeRemint.run,
    };
}

/// Transport where "SESSION" lists projects directly, "ADMIN" is refused by the probe
/// but mints, and an unauthenticated mint succeeds (an open server).
const FakeResolve = struct {
    fn run(
        gpa: std.mem.Allocator,
        io: std.Io,
        url: []const u8,
        method: std.http.Method,
        payload: ?[]const u8,
        headers: []const std.http.Header,
    ) TransportError![]u8 {
        _ = io;
        _ = payload;
        _ = method;
        const auth = FakeRemint.headerValue(headers, "authorization");
        if (std.mem.endsWith(u8, url, "/auth/token")) {
            if (auth.len != 0 and !std.mem.eql(u8, auth, "Bearer ADMIN")) return Error.Unauthorized;
            return gpa.dupe(u8, "{\"token\":\"minted\"}");
        }
        if (std.mem.eql(u8, auth, "Bearer SESSION")) return gpa.dupe(u8, "{\"projects\":[]}");
        return Error.Unauthorized;
    }
};

test "request bodies are built by std.json — a name holding quotes cannot break out" {
    const a = testing.allocator;
    const tricky = "a\"b\\c\nd";
    const payload = try jsonBody(a, .{ .name = tricky, .version = 7 });
    defer a.free(payload);
    try testing.expectEqualStrings("{\"name\":\"a\\\"b\\\\c\\nd\",\"version\":7}", payload);

    // Round-tripping gets the original name back, escapes and all.
    const parsed = try std.json.parseFromSlice(std.json.Value, a, payload, .{});
    defer parsed.deinit();
    try testing.expectEqualStrings(tricky, parsed.value.object.get("name").?.string);
}

test "request re-mints once with the credential and retries a stale session" {
    FakeRemint.calls = 0;
    FakeRemint.mint_ok = true;
    var c = try remintTestClient("ADMIN");
    defer c.deinit();
    const body = try c.listProjects();
    defer testing.allocator.free(body);
    try testing.expectEqualStrings("{\"projects\":[]}", body);
    try testing.expectEqual(@as(usize, 3), FakeRemint.calls); // reject + mint + retry
    try testing.expectEqualStrings("fresh", c.token);
    try testing.expectEqualStrings("Bearer fresh", c.auth);
}

test "request without a credential propagates Unauthorized, no re-mint" {
    FakeRemint.calls = 0;
    FakeRemint.mint_ok = true;
    var c = try remintTestClient("");
    defer c.deinit();
    try testing.expectError(Error.Unauthorized, c.listProjects());
    try testing.expectEqual(@as(usize, 1), FakeRemint.calls);
}

test "a rejected re-mint surfaces the original Unauthorized, no retry loop" {
    FakeRemint.calls = 0;
    FakeRemint.mint_ok = false;
    var c = try remintTestClient("ADMIN");
    defer c.deinit();
    try testing.expectError(Error.Unauthorized, c.listProjects());
    try testing.expectEqual(@as(usize, 2), FakeRemint.calls); // reject + failed mint only
    try testing.expectEqualStrings("stale", c.token); // session left untouched
}

test "a mid-session re-mint proves the credential is an admin token" {
    FakeRemint.calls = 0;
    FakeRemint.mint_ok = true;
    var c = try remintTestClient("ADMIN");
    defer c.deinit();
    c.credential_kind = .session; // what the connect-time probe had concluded
    const body = try c.listProjects();
    defer testing.allocator.free(body);
    try testing.expectEqual(CredentialKind.admin, c.credential_kind);

    // A failed re-mint proves nothing: the kind is left as it was.
    FakeRemint.mint_ok = false;
    var d = try remintTestClient("ADMIN");
    defer d.deinit();
    d.credential_kind = .session;
    try testing.expectError(Error.Unauthorized, d.listProjects());
    try testing.expectEqual(CredentialKind.session, d.credential_kind);
}

test "resolveToken classifies the credential as session, admin, or none" {
    const a = testing.allocator;
    // A token the probe accepts is an ordinary session token, kept as-is.
    var r = try resolveToken(a, undefined, "http://s", "SESSION", FakeResolve.run);
    try testing.expectEqual(CredentialKind.session, r.kind);
    try testing.expectEqualStrings("SESSION", r.token);
    a.free(r.token);

    // Refused by the probe but able to mint: proven admin, running on the minted session.
    r = try resolveToken(a, undefined, "http://s", "ADMIN", FakeResolve.run);
    try testing.expectEqual(CredentialKind.admin, r.kind);
    try testing.expectEqualStrings("minted", r.token);
    a.free(r.token);

    // Nothing supplied: an anonymous mint, so there is no credential to classify.
    r = try resolveToken(a, undefined, "http://s", null, FakeResolve.run);
    try testing.expectEqual(CredentialKind.none, r.kind);
    a.free(r.token);

    // A plain wrong token neither probes nor mints.
    try testing.expectError(Error.Unauthorized, resolveToken(a, undefined, "http://s", "NOPE", FakeResolve.run));
}

test {
    _ = errors;
    _ = urls;
    _ = layout_payload;
    _ = parse;
    _ = editchan;
    _ = timespan;
    _ = http;
    _ = rest;
    _ = meta;
    _ = connection;
}

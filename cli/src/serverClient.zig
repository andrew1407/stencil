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

// The wire surface, re-exported so callers keep one import (server.normalizeBase, …).
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
const testing = std.testing;



/// HTTP seam: rawRequest in production, swappable in tests.
pub const Transport = *const fn (
    gpa: std.mem.Allocator,
    io: std.Io,
    url: []const u8,
    method: std.http.Method,
    payload: ?[]const u8,
    headers: []const std.http.Header,
) TransportError![]u8;

/// What a connection's credential turned out to BE (mirrors the browser's
/// connectionManager credentialKind, with the anonymous case named).
pub const CredentialKind = enum {
    none, // nothing supplied — the session was minted unauthenticated
    session, // the supplied token passed the GET /projects probe directly
    admin, // the supplied token PROVED it can mint a session token
};

pub const Client = struct {
    gpa: std.mem.Allocator,
    io: std.Io,
    base: []u8, // owned, normalized origin
    token: []u8, // owned session token
    auth: []u8, // owned "Bearer <token>"
    credential: []u8, // owned user-supplied token ("" = self-issued); reconnects reuse it
    credential_kind: CredentialKind = .none, // what that credential proved to be
    transport: Transport = rawRequest,

    pub fn deinit(self: *Client) void {
        self.gpa.free(self.base);
        self.gpa.free(self.token);
        self.gpa.free(self.auth);
        self.gpa.free(self.credential);
    }

    /// GET/POST/etc. with the bearer header; returns owned response body bytes.
    /// A stored session dies with a server restart/DB wipe — when connect() was given a
    /// credential, re-mint one session and retry once in place (mirrors the extension).
    fn request(
        self: *Client,
        method: std.http.Method,
        path: []const u8,
        payload: ?[]const u8,
        content_type: ?[]const u8,
    ) ![]u8 {
        return self.send(method, path, payload, content_type) catch |e| {
            if (e != Error.Unauthorized or self.credential.len == 0) return e;
            self.remint() catch return e; // surface the original rejection
            const body = try self.send(method, path, payload, content_type);
            // It minted AND the retried request works: the credential is an admin token.
            self.credential_kind = .admin;
            return body;
        };
    }

    /// One request with the current bearer header; returns owned response body bytes.
    fn send(
        self: *Client,
        method: std.http.Method,
        path: []const u8,
        payload: ?[]const u8,
        content_type: ?[]const u8,
    ) ![]u8 {
        const url = try std.fmt.allocPrint(self.gpa, "{s}{s}", .{ self.base, path });
        defer self.gpa.free(url);
        var headers: [2]std.http.Header = undefined;
        var n: usize = 0;
        headers[n] = .{ .name = "authorization", .value = self.auth };
        n += 1;
        if (content_type) |ct| {
            headers[n] = .{ .name = "content-type", .value = ct };
            n += 1;
        }
        return self.transport(self.gpa, self.io, url, method, payload, headers[0..n]);
    }

    /// POST /auth/token with the stored credential as bearer, swapping in the new session.
    fn remint(self: *Client) !void {
        const url = try std.fmt.allocPrint(self.gpa, "{s}/auth/token", .{self.base});
        defer self.gpa.free(url);
        const bearer = try std.fmt.allocPrint(self.gpa, "Bearer {s}", .{self.credential});
        defer self.gpa.free(bearer);
        const headers = [_]std.http.Header{
            .{ .name = "content-type", .value = "application/json" },
            .{ .name = "authorization", .value = bearer },
        };
        const body = try self.transport(self.gpa, self.io, url, .POST, "{}", &headers);
        defer self.gpa.free(body);
        const token = try parseToken(self.gpa, body);
        errdefer self.gpa.free(token);
        const auth = try std.fmt.allocPrint(self.gpa, "Bearer {s}", .{token});
        self.gpa.free(self.token);
        self.gpa.free(self.auth);
        self.token = token;
        self.auth = auth;
    }

    pub fn listProjects(self: *Client) ![]u8 {
        return self.request(.GET, "/projects", null, null);
    }

    pub fn findProjectIdByName(self: *Client, name: []const u8) !?[]u8 {
        const body = try self.listProjects();
        defer self.gpa.free(body);
        return findIdByName(self.gpa, body, name);
    }

    /// Resolve a project name to its id + current version (for the LWW pull guard).
    pub fn findProjectRef(self: *Client, name: []const u8) !?ProjectRef {
        const body = try self.listProjects();
        defer self.gpa.free(body);
        return findProjectByName(self.gpa, body, name);
    }

    /// List the server's projects as owned ProjectInfo records (free with freeProjectList).
    pub fn listProjectInfos(self: *Client) ![]ProjectInfo {
        const body = try self.listProjects();
        defer self.gpa.free(body);
        return parseProjectList(self.gpa, body);
    }

    pub fn getProject(self: *Client, id: []const u8) ![]u8 {
        const path = try std.fmt.allocPrint(self.gpa, "/projects/{s}", .{id});
        defer self.gpa.free(path);
        return self.request(.GET, path, null, null);
    }

    /// Read just the active project's current server version (used after a push so our
    /// own echoed update event is recognised as ours, not mistaken for a peer's change).
    pub fn getProjectVersion(self: *Client, id: []const u8) !i64 {
        const body = try self.getProject(id);
        defer self.gpa.free(body);
        return parseProjectVersion(self.gpa, body);
    }

    pub fn downloadFile(self: *Client, id: []const u8, kind: []const u8) ![]u8 {
        const path = try std.fmt.allocPrint(self.gpa, "/projects/{s}/files/{s}", .{ id, kind });
        defer self.gpa.free(path);
        return self.request(.GET, path, null, null);
    }

    /// Create a project and return its owned id.
    pub fn createProject(self: *Client, name: []const u8, source: []const u8) ![]u8 {
        const json = try jsonBody(self.gpa, .{ .name = name, .source = source, .hasImage = true });
        defer self.gpa.free(json);
        const body = try self.request(.POST, "/projects", json, "application/json");
        defer self.gpa.free(body);
        return parseProjectId(self.gpa, body);
    }

    /// PUT a layout for `id`, version-guarded (a stale version yields Error.Conflict). The
    /// name is left untouched (omitted from the body). Mirrors the browser/desktop layout
    /// save — the structured `{lines, imageFilter, filterColor, cropRect, rotationQuarters}`
    /// is what open GUI editors render, so this is how CLI edits show up live for peers.
    pub fn updateProject(self: *Client, id: []const u8, layout_json: []const u8, version: i64) !void {
        const path = try std.fmt.allocPrint(self.gpa, "/projects/{s}", .{id});
        defer self.gpa.free(path);
        // `layout_json` is buildLayout's already-serialized envelope, spliced in raw.
        const payload = try std.fmt.allocPrint(self.gpa, "{{\"layout\":{s},\"version\":{d}}}", .{ layout_json, version });
        defer self.gpa.free(payload);
        const body = try self.request(.PUT, path, payload, "application/json");
        self.gpa.free(body);
    }

    /// Read the active project's current custom name colour ("" = none/theme accent).
    pub fn getProjectColor(self: *Client, id: []const u8) ![]u8 {
        const body = try self.getProject(id);
        defer self.gpa.free(body);
        return parseProjectStringField(self.gpa, body, "color");
    }

    /// PUT a project's custom name colour ("#rrggbb" or "" to clear), version-guarded (a stale
    /// version yields Error.Conflict). Mirrors UpdateProjectRequest{color} — only the colour is
    /// sent (the layout/name are left untouched), so it rides the same EventUpdated fan-out.
    pub fn updateProjectColor(self: *Client, id: []const u8, color: []const u8, version: i64) !void {
        const path = try std.fmt.allocPrint(self.gpa, "/projects/{s}", .{id});
        defer self.gpa.free(path);
        const payload = try jsonBody(self.gpa, .{ .color = color, .version = version });
        defer self.gpa.free(payload);
        const body = try self.request(.PUT, path, payload, "application/json");
        self.gpa.free(body);
    }

    /// Read the active project's blank-image fill colour ("" = not a blank project).
    pub fn getProjectBlankColor(self: *Client, id: []const u8) ![]u8 {
        const body = try self.getProject(id);
        defer self.gpa.free(body);
        return parseProjectStringField(self.gpa, body, "blankColor");
    }

    /// PUT a project's blank fill colour ("#rrggbb"), version-guarded (a stale version yields
    /// Error.Conflict). Mirrors UpdateProjectRequest{blankColor} — only the colour is sent, riding
    /// the same EventUpdated fan-out. Note: this recolours the stored blank metadata; the raster
    /// itself is regenerated by the front-end that owns the canvas.
    pub fn updateProjectBlankColor(self: *Client, id: []const u8, color: []const u8, version: i64) !void {
        const path = try std.fmt.allocPrint(self.gpa, "/projects/{s}", .{id});
        defer self.gpa.free(path);
        const payload = try jsonBody(self.gpa, .{ .blankColor = color, .version = version });
        defer self.gpa.free(payload);
        const body = try self.request(.PUT, path, payload, "application/json");
        self.gpa.free(body);
    }

    /// Read the active project's free-text description ("" = none).
    pub fn getProjectDescription(self: *Client, id: []const u8) ![]u8 {
        const body = try self.getProject(id);
        defer self.gpa.free(body);
        return parseProjectStringField(self.gpa, body, "description");
    }

    /// PUT a project's free-text description ("" clears it), version-guarded (a stale version yields
    /// Error.Conflict). Mirrors UpdateProjectRequest{description} — only the description is sent
    /// (name/colour/layout untouched), riding the same EventUpdated fan-out so peers see it live.
    pub fn updateProjectDescription(self: *Client, id: []const u8, description: []const u8, version: i64) !void {
        const path = try std.fmt.allocPrint(self.gpa, "/projects/{s}", .{id});
        defer self.gpa.free(path);
        const payload = try jsonBody(self.gpa, .{ .description = description, .version = version });
        defer self.gpa.free(payload);
        const body = try self.request(.PUT, path, payload, "application/json");
        self.gpa.free(body);
    }

    /// Read the active/named project's current keywords (owned [][]u8; free with freeStrList).
    pub fn getProjectKeywords(self: *Client, id: []const u8) ![][]u8 {
        const body = try self.getProject(id);
        defer self.gpa.free(body);
        return parseProjectKeywords(self.gpa, body);
    }

    /// PUT a project's keywords array, version-guarded (a stale version yields Error.Conflict).
    /// Mirrors UpdateProjectRequest{keywords} — only the keywords are sent (name/colour/layout
    /// untouched), riding the same EventUpdated fan-out so peers see the change live.
    pub fn updateProjectKeywords(self: *Client, id: []const u8, keywords: []const []const u8, version: i64) !void {
        const path = try std.fmt.allocPrint(self.gpa, "/projects/{s}", .{id});
        defer self.gpa.free(path);
        const payload = try jsonBody(self.gpa, .{ .keywords = keywords, .version = version });
        defer self.gpa.free(payload);
        const body = try self.request(.PUT, path, payload, "application/json");
        self.gpa.free(body);
    }

    /// PUT a project's name, version-guarded (a stale version yields Error.Conflict). Mirrors
    /// UpdateProjectRequest{name} — only the name is sent (colour/layout untouched), riding the
    /// same EventUpdated fan-out so peers see the rename live.
    pub fn updateProjectName(self: *Client, id: []const u8, name: []const u8, version: i64) !void {
        const path = try std.fmt.allocPrint(self.gpa, "/projects/{s}", .{id});
        defer self.gpa.free(path);
        const payload = try jsonBody(self.gpa, .{ .name = name, .version = version });
        defer self.gpa.free(payload);
        const body = try self.request(.PUT, path, payload, "application/json");
        self.gpa.free(body);
    }

    /// PUT a project's expiration (epoch ms; 0 = keep forever), version-guarded (a stale
    /// version yields Error.Conflict). Mirrors UpdateProjectRequest{expiresAt} — only the
    /// expiry is sent (name/colour/layout untouched), riding the same EventUpdated fan-out.
    pub fn updateProjectExpiry(self: *Client, id: []const u8, expires_at: i64, version: i64) !void {
        const path = try std.fmt.allocPrint(self.gpa, "/projects/{s}", .{id});
        defer self.gpa.free(path);
        const payload = try jsonBody(self.gpa, .{ .expiresAt = expires_at, .version = version });
        defer self.gpa.free(payload);
        const body = try self.request(.PUT, path, payload, "application/json");
        self.gpa.free(body);
    }

    /// Upload raw image bytes for a project (kind = original|result). The server is
    /// codec-free, so width/height are passed in.
    pub fn uploadFile(self: *Client, id: []const u8, kind: []const u8, bytes: []const u8, ext: []const u8, w: usize, h: usize) !void {
        const path = try std.fmt.allocPrint(
            self.gpa,
            "/projects/{s}/files/{s}?ext={s}&w={d}&h={d}",
            .{ id, kind, ext, w, h },
        );
        defer self.gpa.free(path);
        const body = try self.request(.POST, path, bytes, "application/octet-stream");
        self.gpa.free(body);
    }

    /// DELETE a project's stored file kind (valid for the filestore-only kinds —
    /// video/variantN/chat). Idempotent on the server: a kind with no stored bytes still
    /// answers 204, so a repeat delete is not an error.
    pub fn deleteFile(self: *Client, id: []const u8, kind: []const u8) !void {
        const path = try std.fmt.allocPrint(self.gpa, "/projects/{s}/files/{s}", .{ id, kind });
        defer self.gpa.free(path);
        const body = try self.request(.DELETE, path, null, null);
        self.gpa.free(body);
    }
};

/// Connect to a server: normalize the URL, then either validate the supplied token
/// (a rejected one is retried as the ADMIN credential, minting a session with it —
/// mirrors the desktop's Token field) or issue a fresh one (POST /auth/token).
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
const Resolved = struct { token: []u8, kind: CredentialKind };

/// The session token a connection runs on: a supplied token is validated with a GET
/// /projects probe — one the server rejects is retried as an ADMIN credential (mint a
/// session with it as bearer) — and no token issues a fresh session unauthenticated.
/// `transport` is the HTTP seam (rawRequest in production, a fake in tests).
fn resolveToken(
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
            // Not a session token — but it may be the server's ADMIN token: try minting
            // a session with it. If that fails too, report the probe's rejection (the
            // mint's "admin token required" would misname a plain wrong token).
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
fn issueToken(gpa: std.mem.Allocator, io: std.Io, base: []const u8, auth_opt: ?[]const u8, transport: Transport) ![]u8 {
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

// Zig errors carry no payload, so the most recent non-2xx response's status and
// server-sent message are kept here for connect()'s callers to report. Thread-local: this
// is "the last rejection THIS thread saw", so a fan-out never overwrites the caller's.
threadlocal var reject_status: u32 = 0;
threadlocal var reject_buf: [256]u8 = undefined;
threadlocal var reject_len: usize = 0;

pub const Reject = struct { status: u32, message: []const u8 };

/// The last non-2xx response's status + message, or null when the last request
/// succeeded or never reached the server.
pub fn lastReject() ?Reject {
    if (reject_status == 0) return null;
    return .{ .status = reject_status, .message = reject_buf[0..reject_len] };
}

const RejectCopy = struct { status: u32, buf: [256]u8, len: usize };

fn saveReject() RejectCopy {
    return .{ .status = reject_status, .buf = reject_buf, .len = reject_len };
}

fn restoreReject(r: RejectCopy) void {
    reject_status = r.status;
    reject_buf = r.buf;
    reject_len = r.len;
}

fn recordReject(gpa: std.mem.Allocator, status: u32, body: []const u8) void {
    reject_status = status;
    const msg = parseErrorMessage(gpa, body);
    defer if (msg) |m| gpa.free(m);
    // The server's prose is untrusted: bound it and strip keys/URLs before it can be printed.
    var buf: sanitize.DetailBuf = undefined;
    const src = sanitize.sanitizeDetail(msg orelse body, &buf);
    reject_len = @min(src.len, reject_buf.len);
    @memcpy(reject_buf[0..reject_len], src[0..reject_len]);
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

/// One-shot HTTP request with explicit headers; returns owned response body bytes. Runs over
/// net.request: response capped, redirect refused, host block skipped (the user's own server).
pub fn rawRequest(
    gpa: std.mem.Allocator,
    io: std.Io,
    url: []const u8,
    method: std.http.Method,
    payload: ?[]const u8,
    headers: []const std.http.Header,
) TransportError![]u8 {
    reject_status = 0; // a transport failure below leaves no stale rejection behind
    const opts: net.RequestOptions = .{ .method = method, .payload = payload, .extra_headers = headers, .allow_named_host = true };
    const res = net.request(gpa, io, url, opts) catch |e| return if (e == error.OutOfMemory) error.OutOfMemory else Error.HttpFailed;

    const code = res.status;
    if (code < 200 or code >= 300) {
        defer gpa.free(res.body);
        recordReject(gpa, code, res.body);
        if (code == 401) return Error.Unauthorized;
        if (code == 404) return Error.NotFound;
        if (code == 409) return Error.Conflict;
        return Error.HttpFailed;
    }
    return res.body;
}

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

test "recordReject keeps status + message; falls back to the raw body; save/restore round-trips" {
    const a = testing.allocator;
    recordReject(a, 401, "{\"code\":\"unauthorized\",\"message\":\"missing or invalid token\"}");
    var r = lastReject().?;
    try testing.expectEqual(@as(u32, 401), r.status);
    try testing.expectEqualStrings("missing or invalid token", r.message);

    // A non-JSON body is reported raw (trimmed).
    const saved = saveReject();
    recordReject(a, 503, "  service melting\n");
    r = lastReject().?;
    try testing.expectEqual(@as(u32, 503), r.status);
    try testing.expectEqualStrings("service melting", r.message);

    // restoreReject brings the earlier rejection back (used by the admin-mint retry).
    restoreReject(saved);
    r = lastReject().?;
    try testing.expectEqual(@as(u32, 401), r.status);
    try testing.expectEqualStrings("missing or invalid token", r.message);

    // The server's prose is sanitized on the way in: no key, no URL, and bounded.
    recordReject(a, 500, "{\"code\":\"x\",\"message\":\"upstream http://10.0.0.5:9000/llm rejected sk-abcdef1234567890\"}");
    r = lastReject().?;
    try testing.expectEqualStrings("upstream [redacted] rejected [redacted]", r.message);
    var long: [600]u8 = undefined;
    for (&long, 0..) |*c, i| c.* = if (i % 5 == 4) ' ' else 'a';
    recordReject(a, 500, &long);
    try testing.expect(lastReject().?.message.len <= sanitize.detail_limit + "…".len);

    reject_status = 0; // leave no cross-test state
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
}

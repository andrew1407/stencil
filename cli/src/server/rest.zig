//! The REST client: one connection, its credential and the calls the console and the
//! flag mode make. Re-mints a dead session once in place (mirrors the extension). The
//! per-field project metadata calls live in meta.zig and bind in below as methods.
const std = @import("std");
const errors = @import("errors.zig");
const parse = @import("parse.zig");
const payload_mod = @import("payload.zig");
const http = @import("http.zig");
const meta = @import("meta.zig");

const Error = errors.Error;
const Transport = http.Transport;
const rawRequest = http.rawRequest;
const ProjectRef = parse.ProjectRef;
const ProjectInfo = parse.ProjectInfo;
const parseToken = parse.parseToken;
const parseProjectId = parse.parseProjectId;
const findIdByName = parse.findIdByName;
const findProjectByName = parse.findProjectByName;
const parseProjectVersion = parse.parseProjectVersion;
const parseProjectList = parse.parseProjectList;
const jsonBody = payload_mod.body;

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
    pub fn request(
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
    // The project metadata fields (meta.zig), bound as methods on the client.
    pub const getProjectColor = meta.getProjectColor;
    pub const updateProjectColor = meta.updateProjectColor;
    pub const getProjectBlankColor = meta.getProjectBlankColor;
    pub const updateProjectBlankColor = meta.updateProjectBlankColor;
    pub const getProjectDescription = meta.getProjectDescription;
    pub const updateProjectDescription = meta.updateProjectDescription;
    pub const getProjectKeywords = meta.getProjectKeywords;
    pub const updateProjectKeywords = meta.updateProjectKeywords;
    pub const updateProjectName = meta.updateProjectName;
    pub const updateProjectExpiry = meta.updateProjectExpiry;

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

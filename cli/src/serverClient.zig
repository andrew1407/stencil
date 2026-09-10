//! Stencil collaboration-server client for the CLI. Mirrors server/internal/protocol
//! over REST (through net.request's guarded client) for the non-console runtime:
//! connect/issue a token, find a project by name, download its image, create a project,
//! and upload result bytes. It also opens a read-only live events subscription (`EditConn`)
//! over the server's raw-TCP NDJSON edit channel — TCP rather than a WebSocket
//! library — to learn when a project it is editing was changed by another client.
const std = @import("std");
const logo = @import("logo.zig");
const net = @import("net.zig");
const sanitize = @import("sanitize.zig");

pub const Error = error{
    HttpFailed,
    Unauthorized,
    NotFound,
    Conflict, // 409: a stale version on a layout PUT (a peer saved first) — caller retries
    BadResponse,
    NotConnected,
    TlsNotSupported,
};

pub const TransportError = Error || std.mem.Allocator.Error;

/// HTTP seam: rawRequest in production, swappable in tests.
pub const Transport = *const fn (
    gpa: std.mem.Allocator,
    io: std.Io,
    url: []const u8,
    method: std.http.Method,
    payload: ?[]const u8,
    headers: []const std.http.Header,
) TransportError![]u8;

// ── pure helpers (no network; unit-tested) ───────────────────────────────────

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

/// Build one NDJSON frame: compact JSON + '\n'. Compact JSON never contains a raw
/// newline, so '\n' is an unambiguous delimiter for the TCP edit transport.
pub fn frame(gpa: std.mem.Allocator, json: []const u8) ![]u8 {
    return std.fmt.allocPrint(gpa, "{s}\n", .{json});
}

/// Build the hello frame that opens a TCP/WS edit session (empty project_id selects
/// the global events feed). Caller owns the returned slice.
pub fn helloFrame(gpa: std.mem.Allocator, token: []const u8, project_id: []const u8, client_id: []const u8) ![]u8 {
    return std.fmt.allocPrint(
        gpa,
        "{{\"type\":\"hello\",\"token\":\"{s}\",\"projectId\":\"{s}\",\"clientId\":\"{s}\"}}\n",
        .{ token, project_id, client_id },
    );
}

/// Parse a { "token": "..." } response, returning an owned copy of the token.
pub fn parseToken(gpa: std.mem.Allocator, body: []const u8) ![]u8 {
    const T = struct { token: []const u8 };
    var p = std.json.parseFromSlice(T, gpa, body, .{ .ignore_unknown_fields = true }) catch return Error.BadResponse;
    defer p.deinit();
    return gpa.dupe(u8, p.value.token);
}

/// Parse a created/returned project record { "id": "..." }, returning the owned id.
pub fn parseProjectId(gpa: std.mem.Allocator, body: []const u8) ![]u8 {
    const T = struct { id: []const u8 };
    var p = std.json.parseFromSlice(T, gpa, body, .{ .ignore_unknown_fields = true }) catch return Error.BadResponse;
    defer p.deinit();
    return gpa.dupe(u8, p.value.id);
}

/// Find a project id by (case-insensitive) name in a { "projects": [...] } list body.
pub fn findIdByName(gpa: std.mem.Allocator, body: []const u8, name: []const u8) !?[]u8 {
    const T = struct {
        projects: []const struct { id: []const u8, name: []const u8 },
    };
    var p = std.json.parseFromSlice(T, gpa, body, .{ .ignore_unknown_fields = true }) catch return Error.BadResponse;
    defer p.deinit();
    for (p.value.projects) |proj| {
        if (std.ascii.eqlIgnoreCase(proj.name, name)) return try gpa.dupe(u8, proj.id);
    }
    return null;
}

/// A project reference resolved from a list: its id plus the current server version.
/// The version seeds the console's last-writer-wins guard so it knows which incoming
/// edit events are genuinely newer than what it already holds. Caller owns `id`.
pub const ProjectRef = struct { id: []u8, version: i64 };

/// Like findIdByName, but also captures the project's monotonic edit version.
pub fn findProjectByName(gpa: std.mem.Allocator, body: []const u8, name: []const u8) !?ProjectRef {
    const T = struct {
        projects: []const struct { id: []const u8, name: []const u8, version: i64 = 0 },
    };
    var p = std.json.parseFromSlice(T, gpa, body, .{ .ignore_unknown_fields = true }) catch return Error.BadResponse;
    defer p.deinit();
    for (p.value.projects) |proj| {
        if (std.ascii.eqlIgnoreCase(proj.name, name))
            return ProjectRef{ .id = try gpa.dupe(u8, proj.id), .version = proj.version };
    }
    return null;
}

/// Parse a single-project body ({ "project": { ..., "version": N } }) for its version.
pub fn parseProjectVersion(gpa: std.mem.Allocator, body: []const u8) !i64 {
    const T = struct { project: struct { version: i64 = 0 } };
    var p = std.json.parseFromSlice(T, gpa, body, .{ .ignore_unknown_fields = true }) catch return Error.BadResponse;
    defer p.deinit();
    return p.value.project.version;
}

/// A crop rectangle in rotated-original pixels, for the layout envelope (kept core-free here).
pub const CropRect = struct { x: i64, y: i64, w: i64, h: i64 };

/// Page format + x/y formulas round-tripped through the layout (CLI preserves, doesn't edit).
/// Empty strings / zero dims are omitted from the JSON.
pub const PageMeta = struct {
    page_size: []const u8 = "", // "" | a named format ("A0".."C10") | "custom"
    custom_w: f64 = 0, // cm; 0 = unset
    custom_h: f64 = 0,
    allow_formulas: bool = false,
    formula_x: []const u8 = "", // "" = identity transform
    formula_y: []const u8 = "",
};

/// Append `,"key":"<escaped value>"` to the layout buffer.
fn appendJsonStr(gpa: std.mem.Allocator, out: *std.ArrayList(u8), key: []const u8, val: []const u8) !void {
    const esc = try jsonEscape(gpa, val);
    defer gpa.free(esc);
    const s = try std.fmt.allocPrint(gpa, ",\"{s}\":\"{s}\"", .{ key, esc });
    defer gpa.free(s);
    try out.appendSlice(gpa, s);
}

/// Append `,"key":<number>` to the layout buffer.
fn appendJsonNum(gpa: std.mem.Allocator, out: *std.ArrayList(u8), key: []const u8, val: anytype) !void {
    const s = try std.fmt.allocPrint(gpa, ",\"{s}\":{d}", .{ key, val });
    defer gpa.free(s);
    try out.appendSlice(gpa, s);
}

/// Build a browser-compatible layout JSON envelope:
/// {imageWidth,imageHeight,lines[,imageFilter][,filterColor][,cropRect][,rotationQuarters]
///  [,pageSize][,customPageWidth][,customPageHeight][,allowFormulas][,formulaX][,formulaY]}.
/// `lines_json` is a ready JSON array string ("[]" when empty). The optional fields are
/// omitted when empty/zero, matching browser layout.js buildLayoutPayload. Pure; caller owns.
pub fn buildLayout(
    gpa: std.mem.Allocator,
    w: i64,
    h: i64,
    lines_json: []const u8,
    filter_mode: []const u8,
    filter_color: []const u8,
    crop: ?CropRect,
    rotation: i32,
    meta: PageMeta,
) ![]u8 {
    var out: std.ArrayList(u8) = .empty;
    errdefer out.deinit(gpa);
    const head = try std.fmt.allocPrint(gpa, "{{\"imageWidth\":{d},\"imageHeight\":{d},\"lines\":{s}", .{ w, h, if (lines_json.len == 0) "[]" else lines_json });
    defer gpa.free(head);
    try out.appendSlice(gpa, head);
    if (filter_mode.len != 0 and !std.ascii.eqlIgnoreCase(filter_mode, "none"))
        try appendJsonStr(gpa, &out, "imageFilter", filter_mode);
    if (filter_color.len != 0)
        try appendJsonStr(gpa, &out, "filterColor", filter_color);
    if (crop) |cr| {
        const s = try std.fmt.allocPrint(gpa, ",\"cropRect\":{{\"x\":{d},\"y\":{d},\"width\":{d},\"height\":{d}}}", .{ cr.x, cr.y, cr.w, cr.h });
        defer gpa.free(s);
        try out.appendSlice(gpa, s);
    }
    if (rotation != 0) try appendJsonNum(gpa, &out, "rotationQuarters", rotation);
    if (meta.page_size.len != 0) try appendJsonStr(gpa, &out, "pageSize", meta.page_size);
    if (meta.custom_w != 0) try appendJsonNum(gpa, &out, "customPageWidth", meta.custom_w);
    if (meta.custom_h != 0) try appendJsonNum(gpa, &out, "customPageHeight", meta.custom_h);
    if (meta.allow_formulas) try out.appendSlice(gpa, ",\"allowFormulas\":true");
    if (meta.formula_x.len != 0) try appendJsonStr(gpa, &out, "formulaX", meta.formula_x);
    if (meta.formula_y.len != 0) try appendJsonStr(gpa, &out, "formulaY", meta.formula_y);
    try out.append(gpa, '}');
    return out.toOwnedSlice(gpa);
}

/// One project as shown by `/projects`: name + image size + last-change timestamp, plus the
/// project's custom name colour ("" = none, paint the name in the theme accent) and free-text
/// description ("" = none). Owns `name`, `color`, and `description`.
pub const ProjectInfo = struct { name: []u8, created_at: i64, updated_at: i64, expires_at: i64, w: i64, h: i64, color: []u8, description: []u8, keywords: [][]u8 };

/// Free a slice of owned strings (each string, then the slice). Used for keyword lists.
pub fn freeStrList(gpa: std.mem.Allocator, items: [][]u8) void {
    for (items) |s| gpa.free(s);
    gpa.free(items);
}

/// Dupe a slice of borrowed strings into an owned [][]u8 (free with freeStrList).
fn dupeStrList(gpa: std.mem.Allocator, src: []const []const u8) ![][]u8 {
    var list: std.ArrayList([]u8) = .empty;
    errdefer freeStrList(gpa, list.toOwnedSlice(gpa) catch &.{});
    for (src) |s| try list.append(gpa, try gpa.dupe(u8, s));
    return list.toOwnedSlice(gpa);
}

/// Parse a { "projects": [...] } list body into an owned slice of ProjectInfo. Free with
/// freeProjectList. Pure — unit-tested without a socket.
pub fn parseProjectList(gpa: std.mem.Allocator, body: []const u8) ![]ProjectInfo {
    const T = struct {
        projects: []const struct {
            name: []const u8 = "",
            createdAt: i64 = 0,
            updatedAt: i64 = 0,
            expiresAt: i64 = 0,
            imageW: i64 = 0,
            imageH: i64 = 0,
            color: []const u8 = "",
            description: []const u8 = "",
            keywords: []const []const u8 = &.{},
        },
    };
    var p = std.json.parseFromSlice(T, gpa, body, .{ .ignore_unknown_fields = true }) catch return Error.BadResponse;
    defer p.deinit();
    var list: std.ArrayList(ProjectInfo) = .empty;
    errdefer freeProjectList(gpa, list.toOwnedSlice(gpa) catch &.{});
    for (p.value.projects) |proj| {
        const nm = try gpa.dupe(u8, proj.name);
        errdefer gpa.free(nm);
        const col = try gpa.dupe(u8, proj.color);
        errdefer gpa.free(col);
        const desc = try gpa.dupe(u8, proj.description);
        errdefer gpa.free(desc);
        const kws = try dupeStrList(gpa, proj.keywords);
        errdefer freeStrList(gpa, kws);
        try list.append(gpa, .{ .name = nm, .created_at = proj.createdAt, .updated_at = proj.updatedAt, .expires_at = proj.expiresAt, .w = proj.imageW, .h = proj.imageH, .color = col, .description = desc, .keywords = kws });
    }
    return list.toOwnedSlice(gpa);
}

/// Free a slice returned by parseProjectList (each owned name + colour + description + keywords,
/// then the slice).
pub fn freeProjectList(gpa: std.mem.Allocator, items: []ProjectInfo) void {
    for (items) |it| {
        gpa.free(it.name);
        gpa.free(it.color);
        gpa.free(it.description);
        freeStrList(gpa, it.keywords);
    }
    gpa.free(items);
}

/// Parse a single-project body ({ "project": { ..., "keywords": [...] } }) into an owned
/// [][]u8 (free with freeStrList). "" / absent → empty. Pure — unit-tested without a socket.
pub fn parseProjectKeywords(gpa: std.mem.Allocator, body: []const u8) ![][]u8 {
    const T = struct { project: struct { keywords: []const []const u8 = &.{} } };
    var p = std.json.parseFromSlice(T, gpa, body, .{ .ignore_unknown_fields = true }) catch return Error.BadResponse;
    defer p.deinit();
    return dupeStrList(gpa, p.value.project.keywords);
}

/// Parse one string field out of a single-project body ({ "project": { <key>: "…" } }) —
/// "color", "blankColor" ("" = not a blank project), "description". "" when the key is
/// absent/empty or holds a non-string. Caller owns the returned slice.
pub fn parseProjectStringField(gpa: std.mem.Allocator, body: []const u8, key: []const u8) ![]u8 {
    var p = std.json.parseFromSlice(std.json.Value, gpa, body, .{}) catch return Error.BadResponse;
    defer p.deinit();
    if (p.value != .object) return Error.BadResponse;
    const proj = p.value.object.get("project") orelse return Error.BadResponse;
    if (proj != .object) return Error.BadResponse;
    const v = proj.object.get(key) orelse return gpa.dupe(u8, "");
    return gpa.dupe(u8, if (v == .string) v.string else "");
}

// ── REST client ──────────────────────────────────────────────────────────────

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
        const esc_name = try jsonEscape(self.gpa, name);
        defer self.gpa.free(esc_name);
        const esc_source = try jsonEscape(self.gpa, source);
        defer self.gpa.free(esc_source);
        const json = try std.fmt.allocPrint(
            self.gpa,
            "{{\"name\":\"{s}\",\"source\":\"{s}\",\"hasImage\":true}}",
            .{ esc_name, esc_source },
        );
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
        const esc = try jsonEscape(self.gpa, color);
        defer self.gpa.free(esc);
        const payload = try std.fmt.allocPrint(self.gpa, "{{\"color\":\"{s}\",\"version\":{d}}}", .{ esc, version });
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
        const esc = try jsonEscape(self.gpa, color);
        defer self.gpa.free(esc);
        const payload = try std.fmt.allocPrint(self.gpa, "{{\"blankColor\":\"{s}\",\"version\":{d}}}", .{ esc, version });
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
        const esc = try jsonEscape(self.gpa, description);
        defer self.gpa.free(esc);
        const payload = try std.fmt.allocPrint(self.gpa, "{{\"description\":\"{s}\",\"version\":{d}}}", .{ esc, version });
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
        var arr: std.ArrayList(u8) = .empty;
        defer arr.deinit(self.gpa);
        try arr.append(self.gpa, '[');
        for (keywords, 0..) |k, i| {
            if (i != 0) try arr.append(self.gpa, ',');
            const esc = try jsonEscape(self.gpa, k);
            defer self.gpa.free(esc);
            try arr.append(self.gpa, '"');
            try arr.appendSlice(self.gpa, esc);
            try arr.append(self.gpa, '"');
        }
        try arr.append(self.gpa, ']');
        const payload = try std.fmt.allocPrint(self.gpa, "{{\"keywords\":{s},\"version\":{d}}}", .{ arr.items, version });
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
        const esc = try jsonEscape(self.gpa, name);
        defer self.gpa.free(esc);
        const payload = try std.fmt.allocPrint(self.gpa, "{{\"name\":\"{s}\",\"version\":{d}}}", .{ esc, version });
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
        const payload = try std.fmt.allocPrint(self.gpa, "{{\"expiresAt\":{d},\"version\":{d}}}", .{ expires_at, version });
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
        logo.note("connecting to {s} over plaintext http — your access token and images are sent unencrypted; use https on untrusted networks\n", .{base});

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

// ── last-rejection detail ────────────────────────────────────────────────────
// Zig errors carry no payload, so the most recent non-2xx response's status and
// server-sent message are kept here for connect()'s callers to report.

var reject_status: u32 = 0;
var reject_buf: [256]u8 = undefined;
var reject_len: usize = 0;

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

/// Parse the server's { "code", "message" } error body for its message; null when the
/// body isn't that shape (callers then show the raw body). Caller owns the slice.
pub fn parseErrorMessage(gpa: std.mem.Allocator, body: []const u8) ?[]u8 {
    const T = struct { message: []const u8 };
    var p = std.json.parseFromSlice(T, gpa, body, .{ .ignore_unknown_fields = true }) catch return null;
    defer p.deinit();
    return gpa.dupe(u8, p.value.message) catch null;
}

/// Print why a connect failed: the server's own rejection (status + message) when the
/// last response carried one, else the bare transport error name.
pub fn printConnectError(url: []const u8, e: anyerror) void {
    if (lastReject()) |r| {
        logo.err("server rejected connection ({d}): {s}\n", .{ r.status, r.message });
    } else {
        logo.err("could not connect to {s} ({s})\n", .{ url, @errorName(e) });
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

/// Escape a string for embedding inside a JSON string literal: the two structural chars
/// (`"` and `\`), the common control shorthands (`\n \r \t \b \f`), and any other control
/// byte (< 0x20) as a `\u00XX` sequence. Bytes >= 0x20 (incl. UTF-8 continuation bytes)
/// pass through verbatim, which is valid JSON. Caller owns the returned slice.
fn jsonEscape(gpa: std.mem.Allocator, s: []const u8) ![]u8 {
    var out: std.ArrayList(u8) = .empty;
    errdefer out.deinit(gpa);
    for (s) |ch| {
        switch (ch) {
            '"' => try out.appendSlice(gpa, "\\\""),
            '\\' => try out.appendSlice(gpa, "\\\\"),
            '\n' => try out.appendSlice(gpa, "\\n"),
            '\r' => try out.appendSlice(gpa, "\\r"),
            '\t' => try out.appendSlice(gpa, "\\t"),
            0x08 => try out.appendSlice(gpa, "\\b"),
            0x0c => try out.appendSlice(gpa, "\\f"),
            else => |c| if (c < 0x20) {
                var buf: [6]u8 = undefined;
                const esc = std.fmt.bufPrint(&buf, "\\u{x:0>4}", .{c}) catch unreachable;
                try out.appendSlice(gpa, esc);
            } else {
                try out.append(gpa, c);
            },
        }
    }
    return out.toOwnedSlice(gpa);
}

// ── live edit/events transport (raw TCP, NDJSON) ─────────────────────────────
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

/// A parsed project-update event from the global feed. Caller owns id + name.
/// `version` is the server's monotonic edit counter (used internally as the pull guard,
/// never shown to the user); `updated_at` is the change's epoch-ms timestamp (0 when the
/// frame omits it); `deleted` is true for a "deleted" event rather than an edit.
pub const Event = struct {
    id: []u8,
    name: []u8,
    version: i64,
    updated_at: i64 = 0,
    deleted: bool = false,
    pub fn deinit(self: *Event, gpa: std.mem.Allocator) void {
        gpa.free(self.id);
        gpa.free(self.name);
    }
};

/// Parse one NDJSON frame: returns an owned project-update event, or null for any other
/// frame type / parse failure. Pure — unit-tested without a socket.
pub fn parseEvent(gpa: std.mem.Allocator, json: []const u8) !?Event {
    const T = struct {
        type: []const u8 = "",
        event: []const u8 = "",
        project: ?struct {
            id: []const u8 = "",
            name: []const u8 = "",
            version: i64 = 0,
            updatedAt: i64 = 0,
        } = null,
    };
    var p = std.json.parseFromSlice(T, gpa, json, .{ .ignore_unknown_fields = true }) catch return null;
    defer p.deinit();
    if (!std.mem.eql(u8, p.value.type, "project-event")) return null;
    const proj = p.value.project orelse return null;
    const id = try gpa.dupe(u8, proj.id);
    errdefer gpa.free(id);
    const name = try gpa.dupe(u8, proj.name);
    return Event{
        .id = id,
        .name = name,
        .version = proj.version,
        .updated_at = proj.updatedAt,
        .deleted = std.mem.eql(u8, p.value.event, "deleted"),
    };
}

/// Render an epoch-ms timestamp as a short human "… ago" string relative to `now_ms`,
/// writing into `buf` and returning the used slice (or a static fallback). A zero/missing
/// or future timestamp reads as "just now". Pure — unit-tested.
pub fn formatAgo(buf: []u8, now_ms: i64, then_ms: i64) []const u8 {
    if (then_ms <= 0) return "just now";
    const delta = if (now_ms > then_ms) now_ms - then_ms else 0;
    const secs = @divTrunc(delta, 1000);
    if (secs < 5) return "just now";
    if (secs < 60) return std.fmt.bufPrint(buf, "{d}s ago", .{secs}) catch "moments ago";
    const mins = @divTrunc(secs, 60);
    if (mins < 60) return std.fmt.bufPrint(buf, "{d}m ago", .{mins}) catch "a while ago";
    const hours = @divTrunc(mins, 60);
    if (hours < 24) return std.fmt.bufPrint(buf, "{d}h ago", .{hours}) catch "a while ago";
    return std.fmt.bufPrint(buf, "{d}d ago", .{@divTrunc(hours, 24)}) catch "a while ago";
}

/// Render an expiry timestamp (epoch ms) as a short "in …" / "expired" / "never" string
/// relative to `now_ms`, writing into `buf`. A zero/missing timestamp is "never" (keep
/// forever); an at-or-past one is "expired". Pure — unit-tested. The twin of formatAgo,
/// but forward-looking (expiry reads better as time-until than time-ago).
pub fn formatUntil(buf: []u8, now_ms: i64, then_ms: i64) []const u8 {
    if (then_ms <= 0) return "never";
    if (then_ms <= now_ms) return "expired";
    const delta = then_ms - now_ms;
    const secs = @divTrunc(delta, 1000);
    if (secs < 60) return std.fmt.bufPrint(buf, "in {d}s", .{secs}) catch "soon";
    const mins = @divTrunc(secs, 60);
    if (mins < 60) return std.fmt.bufPrint(buf, "in {d}m", .{mins}) catch "soon";
    const hours = @divTrunc(mins, 60);
    if (hours < 24) return std.fmt.bufPrint(buf, "in {d}h", .{hours}) catch "soon";
    return std.fmt.bufPrint(buf, "in {d}d", .{@divTrunc(hours, 24)}) catch "later";
}

/// A read-only subscription to a server's global project-events feed over the raw-TCP
/// edit channel. Connects, sends a hello, and drains "updated" events without blocking.
pub const EditConn = struct {
    gpa: std.mem.Allocator,
    io: std.Io,
    stream: std.Io.net.Stream,
    rbuf: std.ArrayList(u8) = .empty,
    closed: bool = false,

    /// Connect to the edit port, authenticate with a hello (empty projectId = global
    /// feed), and bound reads with a short receive timeout so draining never stalls.
    pub fn open(gpa: std.mem.Allocator, io: std.Io, base: []const u8, token: []const u8, client_id: []const u8) !EditConn {
        // The events feed is a plaintext TCP socket; it can't speak TLS, so when the
        // server is reached over https (its edit channel is TLS-wrapped too) we skip
        // the live feed rather than dial the wrong port. REST/sync still work over TLS.
        if (std.ascii.startsWithIgnoreCase(base, "https://")) return Error.TlsNotSupported;
        const hp = hostAndPort(base);
        const port = editPort(hp.port);
        // IpAddress.resolve only parses IP LITERALS (it ParseFails on a hostname like
        // "localhost"), so fall back to a DNS lookup via HostName for names. Without this the
        // events feed silently never opened for the common localhost server.
        var stream = if (std.Io.net.IpAddress.resolve(io, hp.host, port)) |lit| s: {
            var addr = lit;
            break :s try addr.connect(io, .{ .mode = .stream });
        } else |_| s: {
            const hn = try std.Io.net.HostName.init(hp.host);
            break :s try hn.connect(io, port, .{ .mode = .stream });
        };
        errdefer stream.close(io);
        const fd = stream.socket.handle;
        // 100ms receive timeout: a drain returns promptly (EAGAIN) when no events pend.
        const tv = std.posix.timeval{ .sec = 0, .usec = 100 * 1000 };
        std.posix.setsockopt(fd, std.posix.SOL.SOCKET, std.posix.SO.RCVTIMEO, std.mem.asBytes(&tv)) catch {};
        const hello = try helloFrame(gpa, token, "", client_id);
        defer gpa.free(hello);
        var wbuf: [256]u8 = undefined;
        var sw = stream.writer(io, &wbuf);
        try sw.interface.writeAll(hello);
        try sw.interface.flush();
        return .{ .gpa = gpa, .io = io, .stream = stream };
    }

    pub fn deinit(self: *EditConn) void {
        self.stream.close(self.io);
        self.rbuf.deinit(self.gpa);
    }

    /// Best-effort drain: read whatever is pending (bounded by the receive timeout), then
    /// pop and return the next complete project-update event, or null when none remain.
    /// Repeated calls drain the buffer one event at a time.
    pub fn poll(self: *EditConn) !?Event {
        if (self.closed) return null;
        if (std.mem.indexOfScalar(u8, self.rbuf.items, '\n') == null) {
            var tmp: [4096]u8 = undefined;
            const n = std.posix.read(self.stream.socket.handle, &tmp) catch |e| switch (e) {
                error.WouldBlock => return null, // receive timeout: nothing pending right now
                else => {
                    self.closed = true;
                    return null;
                },
            };
            if (n == 0) {
                self.closed = true;
                return null;
            }
            try self.feed(tmp[0..n]);
        }
        return self.nextEvent();
    }

    /// Append freshly-read socket bytes to the frame buffer. Split out for testing.
    fn feed(self: *EditConn, data: []const u8) !void {
        try self.rbuf.appendSlice(self.gpa, data);
    }

    /// Pop and parse complete NDJSON frames from the buffer, returning the next project
    /// update event (skipping welcome/synced/other frames), or null when none remain.
    /// Pure buffer work — no socket — so the partial/multi-frame handling is unit-tested.
    fn nextEvent(self: *EditConn) !?Event {
        while (std.mem.indexOfScalar(u8, self.rbuf.items, '\n')) |nl| {
            const line = try self.gpa.dupe(u8, self.rbuf.items[0..nl]);
            defer self.gpa.free(line);
            const rest = self.rbuf.items[nl + 1 ..];
            std.mem.copyForwards(u8, self.rbuf.items, rest);
            self.rbuf.shrinkRetainingCapacity(rest.len);
            if (try parseEvent(self.gpa, line)) |ev| return ev;
        }
        return null;
    }
};

// ── tests (pure helpers) ─────────────────────────────────────────────────────

const testing = std.testing;

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

test "helloFrame and frame are newline-delimited" {
    const a = testing.allocator;
    const h = try helloFrame(a, "tkn", "p_a_b", "c1");
    defer a.free(h);
    try testing.expect(h[h.len - 1] == '\n');
    try testing.expect(std.mem.indexOf(u8, h, "\"projectId\":\"p_a_b\"") != null);
    try testing.expect(std.mem.indexOf(u8, h, "\"token\":\"tkn\"") != null);

    const f = try frame(a, "{\"type\":\"ping\"}");
    defer a.free(f);
    try testing.expectEqualStrings("{\"type\":\"ping\"}\n", f);
}

test "parseToken / parseProjectId" {
    const a = testing.allocator;
    const tok = try parseToken(a, "{\"token\":\"abc123\",\"expiresAt\":0}");
    defer a.free(tok);
    try testing.expectEqualStrings("abc123", tok);

    const id = try parseProjectId(a, "{\"id\":\"p_x_y\",\"name\":\"N\",\"version\":0}");
    defer a.free(id);
    try testing.expectEqualStrings("p_x_y", id);
}

test "jsonEscape escapes quotes, backslashes, and control chars" {
    const a = testing.allocator;

    // Plain text is unchanged (but still owned/allocated).
    const plain = try jsonEscape(a, "My Project");
    defer a.free(plain);
    try testing.expectEqualStrings("My Project", plain);

    // Quotes and backslashes — the bytes that would break the surrounding JSON literal.
    const q = try jsonEscape(a, "a\"b\\c");
    defer a.free(q);
    try testing.expectEqualStrings("a\\\"b\\\\c", q);

    // Whitespace control shorthands.
    const ws = try jsonEscape(a, "x\n\ty\r");
    defer a.free(ws);
    try testing.expectEqualStrings("x\\n\\ty\\r", ws);

    // Other control bytes become \u00XX; bytes >= 0x20 (incl. UTF-8) pass through.
    const ctrl = try jsonEscape(a, "\x01\x1f\u{00e9}");
    defer a.free(ctrl);
    try testing.expectEqualStrings("\\u0001\\u001f\u{00e9}", ctrl);

    // The escaped result must round-trip through a strict JSON parser back to the input.
    const tricky = "name \"with\" \\slashes\\ and\tcontrol\x02";
    const esc = try jsonEscape(a, tricky);
    defer a.free(esc);
    const body = try std.fmt.allocPrint(a, "{{\"v\":\"{s}\"}}", .{esc});
    defer a.free(body);
    const T = struct { v: []const u8 };
    var p = try std.json.parseFromSlice(T, a, body, .{});
    defer p.deinit();
    try testing.expectEqualStrings(tricky, p.value.v);
}

test "parseErrorMessage reads the server's error body, null for other shapes" {
    const a = testing.allocator;
    const msg = parseErrorMessage(a, "{\"code\":\"unauthorized\",\"message\":\"admin token required to issue tokens\"}").?;
    defer a.free(msg);
    try testing.expectEqualStrings("admin token required to issue tokens", msg);

    try testing.expect(parseErrorMessage(a, "not json") == null);
    try testing.expect(parseErrorMessage(a, "{\"code\":\"x\"}") == null); // no message field
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

test "parseEvent returns updated project-events, ignores other frames" {
    const a = testing.allocator;

    // A project-event frame yields an owned id/name/version + the change timestamp.
    const body = "{\"type\":\"project-event\",\"event\":\"updated\",\"project\":{\"id\":\"p_x_y\",\"name\":\"Notes\",\"version\":7,\"updatedAt\":1700000000000}}";
    var ev = (try parseEvent(a, body)).?;
    defer ev.deinit(a);
    try testing.expectEqualStrings("p_x_y", ev.id);
    try testing.expectEqualStrings("Notes", ev.name);
    try testing.expectEqual(@as(i64, 7), ev.version);
    try testing.expectEqual(@as(i64, 1700000000000), ev.updated_at);
    try testing.expect(!ev.deleted);

    // A "deleted" event is flagged; updatedAt absent defaults to 0.
    var del = (try parseEvent(a, "{\"type\":\"project-event\",\"event\":\"deleted\",\"project\":{\"id\":\"p_z\",\"name\":\"Gone\",\"version\":3}}")).?;
    defer del.deinit(a);
    try testing.expect(del.deleted);
    try testing.expectEqual(@as(i64, 0), del.updated_at);

    // Non-event frames (welcome, synced, hello echoes) are ignored.
    try testing.expect((try parseEvent(a, "{\"type\":\"welcome\",\"version\":1}")) == null);
    try testing.expect((try parseEvent(a, "{\"type\":\"project-event\"}")) == null); // no project
    try testing.expect((try parseEvent(a, "not json")) == null);
}

test "formatAgo renders short relative times, just-now for fresh/unknown/future" {
    var buf: [32]u8 = undefined;
    const now: i64 = 1_000_000_000_000;
    try testing.expectEqualStrings("just now", formatAgo(&buf, now, 0)); // missing timestamp
    try testing.expectEqualStrings("just now", formatAgo(&buf, now, now + 5000)); // future clamps
    try testing.expectEqualStrings("just now", formatAgo(&buf, now, now - 2000)); // < 5s
    try testing.expectEqualStrings("30s ago", formatAgo(&buf, now, now - 30_000));
    try testing.expectEqualStrings("5m ago", formatAgo(&buf, now, now - 5 * 60_000));
    try testing.expectEqualStrings("3h ago", formatAgo(&buf, now, now - 3 * 60 * 60_000));
    try testing.expectEqualStrings("2d ago", formatAgo(&buf, now, now - 2 * 24 * 60 * 60_000));
}

test "formatUntil renders forward expiry, never/expired at the edges" {
    var buf: [32]u8 = undefined;
    const now: i64 = 1_000_000_000_000;
    try testing.expectEqualStrings("never", formatUntil(&buf, now, 0)); // keep forever
    try testing.expectEqualStrings("expired", formatUntil(&buf, now, now)); // at boundary
    try testing.expectEqualStrings("expired", formatUntil(&buf, now, now - 1000)); // past
    try testing.expectEqualStrings("in 30s", formatUntil(&buf, now, now + 30_000));
    try testing.expectEqualStrings("in 5m", formatUntil(&buf, now, now + 5 * 60_000));
    try testing.expectEqualStrings("in 3h", formatUntil(&buf, now, now + 3 * 60 * 60_000));
    try testing.expectEqualStrings("in 2d", formatUntil(&buf, now, now + 2 * 24 * 60 * 60_000));
}

test "findProjectByName captures id + version; parseProjectVersion reads a single project" {
    const a = testing.allocator;
    const list =
        "{\"projects\":[{\"id\":\"p_1_a\",\"name\":\"Alpha\",\"version\":4},{\"id\":\"p_2_b\",\"name\":\"Beta\",\"version\":9}]}";
    const ref = (try findProjectByName(a, list, "beta")).?;
    defer a.free(ref.id);
    try testing.expectEqualStrings("p_2_b", ref.id);
    try testing.expectEqual(@as(i64, 9), ref.version);
    try testing.expect((try findProjectByName(a, list, "missing")) == null);

    const v = try parseProjectVersion(a, "{\"project\":{\"id\":\"p_2_b\",\"name\":\"Beta\",\"version\":9}}");
    try testing.expectEqual(@as(i64, 9), v);
}

test "buildLayout emits optional fields only when set" {
    const a = testing.allocator;
    // Bare: just dims + empty lines (no filter/crop/rotation/page/formula).
    const bare = try buildLayout(a, 10, 20, "", "none", "", null, 0, .{});
    defer a.free(bare);
    try testing.expectEqualStrings("{\"imageWidth\":10,\"imageHeight\":20,\"lines\":[]}", bare);

    // Full: filter + color + crop + rotation, with a ready lines array passed through.
    const full = try buildLayout(a, 5, 6, "[{\"x\":1}]", "custom", "#7c3aed", .{ .x = 1, .y = 2, .w = 3, .h = 4 }, 3, .{});
    defer a.free(full);
    try testing.expect(std.mem.indexOf(u8, full, "\"lines\":[{\"x\":1}]") != null);
    try testing.expect(std.mem.indexOf(u8, full, "\"imageFilter\":\"custom\"") != null);
    try testing.expect(std.mem.indexOf(u8, full, "\"filterColor\":\"#7c3aed\"") != null);
    try testing.expect(std.mem.indexOf(u8, full, "\"cropRect\":{\"x\":1,\"y\":2,\"width\":3,\"height\":4}") != null);
    try testing.expect(std.mem.indexOf(u8, full, "\"rotationQuarters\":3") != null);
}

test "buildLayout round-trips page format + formulas (omit-when-default)" {
    const a = testing.allocator;
    // A custom page + x/y formulas survive into the envelope.
    const meta = PageMeta{
        .page_size = "custom",
        .custom_w = 15,
        .custom_h = 25,
        .allow_formulas = true,
        .formula_x = "x*2",
        .formula_y = "y+1",
    };
    const got = try buildLayout(a, 1, 1, "", "none", "", null, 0, meta);
    defer a.free(got);
    try testing.expect(std.mem.indexOf(u8, got, "\"pageSize\":\"custom\"") != null);
    try testing.expect(std.mem.indexOf(u8, got, "\"customPageWidth\":15") != null);
    try testing.expect(std.mem.indexOf(u8, got, "\"customPageHeight\":25") != null);
    try testing.expect(std.mem.indexOf(u8, got, "\"allowFormulas\":true") != null);
    try testing.expect(std.mem.indexOf(u8, got, "\"formulaX\":\"x*2\"") != null);
    try testing.expect(std.mem.indexOf(u8, got, "\"formulaY\":\"y+1\"") != null);

    // A named page with formulas off: pageSize kept, no formula keys, no allowFormulas.
    const named = try buildLayout(a, 1, 1, "", "none", "", null, 0, .{ .page_size = "A4" });
    defer a.free(named);
    try testing.expect(std.mem.indexOf(u8, named, "\"pageSize\":\"A4\"") != null);
    try testing.expect(std.mem.indexOf(u8, named, "allowFormulas") == null);
    try testing.expect(std.mem.indexOf(u8, named, "formulaX") == null);
}

test "parseProjectList yields owned name/size/updatedAt/color records" {
    const a = testing.allocator;
    const body =
        "{\"projects\":[{\"id\":\"p1\",\"name\":\"Alpha\",\"imageW\":800,\"imageH\":600,\"updatedAt\":1700000000000,\"expiresAt\":1700009999000,\"color\":\"#ff5623\",\"description\":\"lead shot\"}," ++
        "{\"id\":\"p2\",\"name\":\"Beta\",\"imageW\":1024,\"imageH\":768,\"updatedAt\":0}]}";
    const items = try parseProjectList(a, body);
    defer freeProjectList(a, items);
    try testing.expectEqual(@as(usize, 2), items.len);
    try testing.expectEqualStrings("Alpha", items[0].name);
    try testing.expectEqual(@as(i64, 800), items[0].w);
    try testing.expectEqual(@as(i64, 600), items[0].h);
    try testing.expectEqual(@as(i64, 1700000000000), items[0].updated_at);
    try testing.expectEqual(@as(i64, 1700009999000), items[0].expires_at);
    try testing.expectEqualStrings("#ff5623", items[0].color);
    try testing.expectEqualStrings("lead shot", items[0].description);
    try testing.expectEqualStrings("Beta", items[1].name);
    try testing.expectEqual(@as(i64, 0), items[1].expires_at); // no expiry → 0 (never)
    try testing.expectEqualStrings("", items[1].color); // no custom colour → empty
    try testing.expectEqualStrings("", items[1].description); // no description → empty

    // An empty list parses to an empty (non-null) slice.
    const none = try parseProjectList(a, "{\"projects\":[]}");
    defer freeProjectList(a, none);
    try testing.expectEqual(@as(usize, 0), none.len);
}

test "parseProjectStringField reads one project field, empty when absent" {
    const a = testing.allocator;
    const body = "{\"project\":{\"id\":\"p_1\",\"name\":\"N\",\"color\":\"#7c3aed\",\"blankColor\":\"#fff\",\"description\":\"a caption\"}}";
    for ([_][2][]const u8{
        .{ "color", "#7c3aed" },
        .{ "blankColor", "#fff" },
        .{ "description", "a caption" },
    }) |c| {
        const got = try parseProjectStringField(a, body, c[0]);
        defer a.free(got);
        try testing.expectEqualStrings(c[1], got);
    }

    const none = try parseProjectStringField(a, "{\"project\":{\"id\":\"p_1\",\"name\":\"N\"}}", "color");
    defer a.free(none);
    try testing.expectEqualStrings("", none);

    try testing.expectError(Error.BadResponse, parseProjectStringField(a, "{\"nope\":1}", "color"));
}

test "EditConn frame buffer handles partial, multiple, and skipped frames" {
    const a = testing.allocator;
    // io/stream are unused by feed/nextEvent (pure buffer work), so leave them undefined.
    var c = EditConn{ .gpa = a, .io = undefined, .stream = undefined };
    defer c.rbuf.deinit(a);

    // A frame split across two reads yields nothing until the newline arrives.
    try c.feed("{\"type\":\"project-event\",\"event\":\"updated\",\"project\":{\"id\":\"p1\",\"nam");
    try testing.expect((try c.nextEvent()) == null);

    // Completing it, plus a non-event frame and a second event, all in one chunk.
    try c.feed("e\":\"A\",\"version\":2}}\n{\"type\":\"welcome\",\"version\":1}\n" ++
        "{\"type\":\"project-event\",\"event\":\"updated\",\"project\":{\"id\":\"p2\",\"name\":\"B\",\"version\":5}}\n");

    var e1 = (try c.nextEvent()).?;
    defer e1.deinit(a);
    try testing.expectEqualStrings("p1", e1.id);
    try testing.expectEqual(@as(i64, 2), e1.version);

    // The welcome frame is skipped; the next event is p2.
    var e2 = (try c.nextEvent()).?;
    defer e2.deinit(a);
    try testing.expectEqualStrings("p2", e2.id);
    try testing.expectEqual(@as(i64, 5), e2.version);

    // Buffer drained.
    try testing.expect((try c.nextEvent()) == null);
}

test "findIdByName matches case-insensitively, else null" {
    const a = testing.allocator;
    const body =
        "{\"projects\":[{\"id\":\"p_1_a\",\"name\":\"Alpha\"},{\"id\":\"p_2_b\",\"name\":\"Beta\"}]}";
    const id = (try findIdByName(a, body, "beta")).?;
    defer a.free(id);
    try testing.expectEqualStrings("p_2_b", id);

    try testing.expect((try findIdByName(a, body, "missing")) == null);
}

// ── mid-session re-mint (fake transport) ─────────────────────────────────────

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

// ── credential kind at connect time (fake transport) ─────────────────────────

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

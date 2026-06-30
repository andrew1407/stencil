//! Stencil collaboration-server client for the CLI. Mirrors server/internal/protocol
//! over REST (std.http.Client) for the non-console runtime: connect/issue a token,
//! find a project by name, download its image, create a project, and upload result
//! bytes. It also opens a read-only live events subscription (`EditConn`) over the
//! server's raw-TCP NDJSON edit channel — the CLI uses TCP rather than a WebSocket
//! library — to learn when a project it is editing was changed by another client.
const std = @import("std");

pub const Error = error{
    HttpFailed,
    Unauthorized,
    NotFound,
    Conflict, // 409: a stale version on a layout PUT (a peer saved first) — caller retries
    BadResponse,
    NotConnected,
    TlsNotSupported,
};

// ── pure helpers (no network; unit-tested) ───────────────────────────────────

/// Normalize a server URL to a clean origin: add http:// if no scheme, drop any
/// path/trailing slash. Caller owns the returned slice.
/// Extract the host from a bare authority ("host:port", "[::1]:port", or "host").
fn bareHost(s: []const u8) []const u8 {
    if (s.len > 0 and s[0] == '[') {
        if (std.mem.indexOfScalar(u8, s, ']')) |close| return s[1..close];
    }
    if (std.mem.indexOfScalar(u8, s, ':')) |colon| return s[0..colon];
    return s;
}

/// True for a loopback host (localhost, *.localhost, 127.0.0.0/8, ::1), where plaintext
/// http is safe because the bytes never leave the machine.
pub fn isLoopbackHost(raw: []const u8) bool {
    // Strip any IPv6 brackets so "[::1]" matches (mirrors the browser/desktop classifiers).
    const host = if (raw.len >= 2 and raw[0] == '[' and raw[raw.len - 1] == ']') raw[1 .. raw.len - 1] else raw;
    if (std.ascii.eqlIgnoreCase(host, "localhost")) return true;
    if (host.len > ".localhost".len and std.ascii.eqlIgnoreCase(host[host.len - ".localhost".len ..], ".localhost")) return true;
    if (std.mem.eql(u8, host, "::1")) return true;
    if (std.mem.startsWith(u8, host, "127.")) return true;
    return false;
}

/// True when `base` would send the bearer token + image bytes in CLEARTEXT to a remote
/// host (http scheme and not loopback) — connect() warns on these.
pub fn isInsecureRemote(base: []const u8) bool {
    if (!std.ascii.startsWithIgnoreCase(base, "http://")) return false;
    return !isLoopbackHost(hostAndPort(base).host);
}

pub fn normalizeBase(gpa: std.mem.Allocator, url: []const u8) ![]u8 {
    var s = std.mem.trim(u8, url, " \t\r\n");
    var buf: []u8 = undefined;
    var owned = false;
    if (!std.ascii.startsWithIgnoreCase(s, "http://") and !std.ascii.startsWithIgnoreCase(s, "https://")) {
        // Secure by default: a bare REMOTE host gets https; loopback keeps plaintext http
        // (localhost dev servers, traffic never leaves the machine). An explicit scheme is
        // preserved, so "http://<remote>" still works — the user opts into cleartext.
        const scheme = if (isLoopbackHost(bareHost(s))) "http://" else "https://";
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
    page_size: []const u8 = "", // "" | "A3" | "A4" | "custom"
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
/// project's custom name colour ("" = none, paint the name in the theme accent). Owns `name`
/// and `color`.
pub const ProjectInfo = struct { name: []u8, updated_at: i64, w: i64, h: i64, color: []u8 };

/// Parse a { "projects": [...] } list body into an owned slice of ProjectInfo. Free with
/// freeProjectList. Pure — unit-tested without a socket.
pub fn parseProjectList(gpa: std.mem.Allocator, body: []const u8) ![]ProjectInfo {
    const T = struct {
        projects: []const struct {
            name: []const u8 = "",
            updatedAt: i64 = 0,
            imageW: i64 = 0,
            imageH: i64 = 0,
            color: []const u8 = "",
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
        try list.append(gpa, .{ .name = nm, .updated_at = proj.updatedAt, .w = proj.imageW, .h = proj.imageH, .color = col });
    }
    return list.toOwnedSlice(gpa);
}

/// Free a slice returned by parseProjectList (each owned name + colour, then the slice).
pub fn freeProjectList(gpa: std.mem.Allocator, items: []ProjectInfo) void {
    for (items) |it| {
        gpa.free(it.name);
        gpa.free(it.color);
    }
    gpa.free(items);
}

/// Parse a single-project body ({ "project": { ..., "color": "#rrggbb" } }) for its custom
/// name colour ("" when absent/empty). Caller owns the returned slice.
pub fn parseProjectColor(gpa: std.mem.Allocator, body: []const u8) ![]u8 {
    const T = struct { project: struct { color: []const u8 = "" } };
    var p = std.json.parseFromSlice(T, gpa, body, .{ .ignore_unknown_fields = true }) catch return Error.BadResponse;
    defer p.deinit();
    return gpa.dupe(u8, p.value.project.color);
}

// ── REST client ──────────────────────────────────────────────────────────────

pub const Client = struct {
    gpa: std.mem.Allocator,
    io: std.Io,
    base: []u8, // owned, normalized origin
    token: []u8, // owned
    auth: []u8, // owned "Bearer <token>"

    pub fn deinit(self: *Client) void {
        self.gpa.free(self.base);
        self.gpa.free(self.token);
        self.gpa.free(self.auth);
    }

    /// GET/POST/etc. with the bearer header; returns owned response body bytes.
    fn request(
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
        return rawRequest(self.gpa, self.io, url, method, payload, headers[0..n]);
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
        return parseProjectColor(self.gpa, body);
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
};

/// Connect to a server: normalize the URL, then either validate the supplied token
/// (GET /projects) or issue a fresh one (POST /auth/token).
pub fn connect(gpa: std.mem.Allocator, io: std.Io, url: []const u8, token_opt: ?[]const u8) !Client {
    const base = try normalizeBase(gpa, url);
    errdefer gpa.free(base);
    if (isInsecureRemote(base))
        std.debug.print("warning: connecting to {s} over plaintext http — your access token and images are sent unencrypted; use https on untrusted networks\n", .{base});

    var token: []u8 = undefined;
    if (token_opt) |t| {
        token = try gpa.dupe(u8, t);
    } else {
        const issue_url = try std.fmt.allocPrint(gpa, "{s}/auth/token", .{base});
        defer gpa.free(issue_url);
        const body = rawRequest(gpa, io, issue_url, .POST, "{}", &.{.{ .name = "content-type", .value = "application/json" }}) catch return Error.HttpFailed;
        defer gpa.free(body);
        token = try parseToken(gpa, body);
    }
    errdefer gpa.free(token);
    const auth = try std.fmt.allocPrint(gpa, "Bearer {s}", .{token});
    errdefer gpa.free(auth);

    var client = Client{ .gpa = gpa, .io = io, .base = base, .token = token, .auth = auth };
    if (token_opt != null) {
        // Validate the supplied token.
        const body = try client.listProjects();
        gpa.free(body);
    }
    return client;
}

/// One-shot HTTP request with explicit headers; returns owned response body bytes.
fn rawRequest(
    gpa: std.mem.Allocator,
    io: std.Io,
    url: []const u8,
    method: std.http.Method,
    payload: ?[]const u8,
    headers: []const std.http.Header,
) ![]u8 {
    var client: std.http.Client = .{ .allocator = gpa, .io = io };
    defer client.deinit();
    var body: std.Io.Writer.Allocating = .init(gpa);
    defer body.deinit();

    const result = client.fetch(.{
        .location = .{ .url = url },
        .method = method,
        .payload = payload,
        .extra_headers = headers,
        .response_writer = &body.writer,
    }) catch return Error.HttpFailed;

    const code = @intFromEnum(result.status);
    if (code == 401) return Error.Unauthorized;
    if (code == 404) return Error.NotFound;
    if (code == 409) return Error.Conflict;
    if (code < 200 or code >= 300) return Error.HttpFailed;
    return gpa.dupe(u8, body.written());
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
    const https = std.ascii.startsWithIgnoreCase(base, "https://");
    const def: u16 = if (https) 443 else 80;
    const scheme_end = (std.mem.indexOf(u8, base, "://") orelse return .{ .host = base, .port = def }) + 3;
    const authority = base[scheme_end..];
    if (std.mem.lastIndexOfScalar(u8, authority, ':')) |i| {
        const port = std.fmt.parseInt(u16, authority[i + 1 ..], 10) catch return .{ .host = authority, .port = def };
        return .{ .host = authority[0..i], .port = port };
    }
    return .{ .host = authority, .port = def };
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

test "isLoopbackHost and isInsecureRemote classify the connection" {
    try testing.expect(isLoopbackHost("localhost"));
    try testing.expect(isLoopbackHost("127.0.0.1"));
    try testing.expect(isLoopbackHost("::1"));
    try testing.expect(isLoopbackHost("[::1]"));  // bracketed IPv6 (parity with the other front-ends)
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
        "{\"projects\":[{\"id\":\"p1\",\"name\":\"Alpha\",\"imageW\":800,\"imageH\":600,\"updatedAt\":1700000000000,\"color\":\"#ff5623\"}," ++
        "{\"id\":\"p2\",\"name\":\"Beta\",\"imageW\":1024,\"imageH\":768,\"updatedAt\":0}]}";
    const items = try parseProjectList(a, body);
    defer freeProjectList(a, items);
    try testing.expectEqual(@as(usize, 2), items.len);
    try testing.expectEqualStrings("Alpha", items[0].name);
    try testing.expectEqual(@as(i64, 800), items[0].w);
    try testing.expectEqual(@as(i64, 600), items[0].h);
    try testing.expectEqual(@as(i64, 1700000000000), items[0].updated_at);
    try testing.expectEqualStrings("#ff5623", items[0].color);
    try testing.expectEqualStrings("Beta", items[1].name);
    try testing.expectEqualStrings("", items[1].color); // no custom colour → empty

    // An empty list parses to an empty (non-null) slice.
    const none = try parseProjectList(a, "{\"projects\":[]}");
    defer freeProjectList(a, none);
    try testing.expectEqual(@as(usize, 0), none.len);
}

test "parseProjectColor reads a single project's colour, empty when absent" {
    const a = testing.allocator;
    const c = try parseProjectColor(a, "{\"project\":{\"id\":\"p_1\",\"name\":\"N\",\"color\":\"#7c3aed\"}}");
    defer a.free(c);
    try testing.expectEqualStrings("#7c3aed", c);

    const none = try parseProjectColor(a, "{\"project\":{\"id\":\"p_1\",\"name\":\"N\"}}");
    defer a.free(none);
    try testing.expectEqualStrings("", none);
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

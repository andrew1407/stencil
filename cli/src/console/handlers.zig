//! Console command implementations: one handler per verb/transform, driving the same
//! pipeline.zig building blocks the flag mode uses, snapshotting into the session's undo
//! history, and reporting via ui.zig. Pure parsing lives in commands.zig.
const std = @import("std");
const image = @import("../image.zig");
const pipeline = @import("../pipeline.zig");
const net = @import("../net.zig");
const scrape = @import("../scrape.zig");
const server = @import("../serverClient.zig");
const logo = @import("../logo.zig");
const core = @import("../core.zig");
const theme = @import("../theme.zig");
const clipboard = @import("../clipboard.zig");
const commands = @import("commands.zig");
const line_edit = @import("../line_edit.zig");
const llm = @import("../llm.zig");
const layout_mod = @import("../layout.zig");
const project = @import("../project.zig");
const ui = @import("ui.zig");
const screen = @import("screen.zig");
const Session = @import("session.zig").Session;
const Attachment = @import("session.zig").Attachment;
const Action = commands.Action;
const projectsTable = @import("projectsTable.zig");
const remoteEvents = @import("remoteEvents.zig");
const attachments = @import("attachments.zig");

// ── source / save ─────────────────────────────────────────────────────────────

pub fn doUpload(session: *Session, io: std.Io, arg: []const u8) !void {
    if (arg.len == 0) {
        // A bare /upload takes the CLIPBOARD's picture when there is one — the same image
        // Ctrl-V attaches — so a copied screenshot needs no path typed at all.
        if (try attachments.clipboardToImage(session, io, true)) return;
        logo.err("upload needs a path or URL — e.g. '/upload photo.png' (or copy an image and run a bare '/upload')\n", .{});
        return;
    }
    // A whole .stencil project loads its image + layout (crop/rotation/filter/lines) at once.
    if (project.isStencilPath(arg)) return openProject(session, io, arg);
    const src = pipeline.acquireInput(session.gpa, io, arg, 0) catch return; // message already printed
    // §2.1: the uploads of one turn are its attachments — a later /prompt sends them all
    // and an `image` op indexes them. Best-effort: a copy we can't afford just isn't one.
    const att_bytes: ?[]u8 = session.gpa.dupe(u8, src.bytes) catch null;
    errdefer if (att_bytes) |b| session.gpa.free(b);
    const temp = net.isUrl(arg);
    try session.loadImage(src.img, arg, temp, src.default_fmt, src.bytes);
    if (att_bytes) |b| session.addAttachment(arg, b, src.default_fmt, temp) catch {};
    ui.redraw(session);
}

/// `/upload <file>.stencil` — load a portable project: decode its embedded ORIGINAL image and
/// adopt its layout (crop/rotation/filter/lines) so the view matches the browser/desktop editors.
pub fn openProject(session: *Session, io: std.Io, path: []const u8) !void {
    var proj = project.loadInto(session, io, path) catch return; // message already printed
    proj.deinit();
    ui.redraw(session);
}

/// `/source-upload <url> [index=0] [format=all] [minW=-1] [maxW=-1] [minH=-1] [maxH=-1]`
/// (alias `/scrape`) — scrape a page, filter media by format + dimensions, and load the
/// item at 0-based `index` as the working image. `-1` = unset bound; `all` = any format.
pub fn doSourceUpload(session: *Session, io: std.Io, arg: []const u8) !void {
    if (arg.len == 0) {
        logo.err("source-upload needs a URL — e.g. '/source-upload https://example.com'\n", .{});
        return;
    }
    const o = parseSourceUpload(arg) orelse {
        logo.err("source-upload takes '<url> [index=0] [format=all] [minW=-1] [maxW=-1] [minH=-1] [maxH=-1]'\n", .{});
        return;
    };
    // The fetch + download can take a moment; announce it up front (parity with the one-shot
    // scrape's leading line and pystencil's _cmd_source_upload) so the console isn't silent.
    logo.print("scraping {s}…\n", .{o.url});
    const loaded = scrape.scrapeOne(session.gpa, io, o) catch return; // message already printed
    defer session.gpa.free(loaded.url);
    // A `name=` token overrides the URL-derived label (parity with pystencil's name=).
    const label = if (o.name.len != 0) o.name else loaded.url;
    try session.loadImage(loaded.img, label, true, loaded.fmt, null);
    ui.redraw(session);
}

/// Parse the `/source-upload` positional grammar; null on a malformed token. `-1` min/max
/// bounds become unset (null); a bare `all` format means any. An optional `name=<label>`
/// key token may appear anywhere after the URL and sets a custom label for the loaded image.
fn parseSourceUpload(arg: []const u8) ?scrape.ConsoleOpts {
    var it = std.mem.tokenizeAny(u8, arg, " \t");
    const url = it.next() orelse return null;
    var o = scrape.ConsoleOpts{ .url = url };
    // Positionals: [index] [format] [minW] [maxW] [minH] [maxH]; a `name=` token is pulled
    // out first (anywhere) so it doesn't consume a positional slot.
    var pos: [6]?[]const u8 = .{ null, null, null, null, null, null };
    var np: usize = 0;
    while (it.next()) |t| {
        if (std.mem.startsWith(u8, t, "name=")) {
            o.name = t["name=".len..];
            continue;
        }
        if (np >= pos.len) return null; // trailing junk
        pos[np] = t;
        np += 1;
    }
    if (pos[0]) |t| o.index = std.fmt.parseInt(u32, t, 10) catch return null;
    if (pos[1]) |t| o.format = t;
    o.min_width = parseBound(pos[2]) catch return null;
    o.max_width = parseBound(pos[3]) catch return null;
    o.min_height = parseBound(pos[4]) catch return null;
    o.max_height = parseBound(pos[5]) catch return null;
    return o;
}

/// A `/source-upload` dimension bound token: `-1` (or absent) → unset (null); else a u32.
fn parseBound(tok: ?[]const u8) !?u32 {
    const t = tok orelse return null;
    const v = try std.fmt.parseInt(i64, t, 10);
    if (v < 0) return null;
    return @intCast(v);
}

pub fn doBlank(session: *Session, arg: []const u8) !void {
    var blank = commands.parseBlank(session.gpa, arg) orelse {
        logo.err("blank takes '[format] [w h] [color]' (a page format and explicit dims are exclusive) — e.g. '/blank 800 600 white' or '/blank b5 pink'\n", .{});
        return;
    };
    // Capture the session's /format pick before the load wipes it (loadImage → clearAll →
    // clearFormat). The canonical slice is static (core-owned), so it survives the load.
    const prev_page: ?[]const u8 = core.canonicalPageFormat(session.page_size);
    const prev_custom = std.ascii.eqlIgnoreCase(session.page_size, "custom");
    const prev_w = session.custom_page_w;
    const prev_h = session.custom_page_h;
    // A bare size (no format, no dims) defaults to the session's picked page format (set via
    // /format or a fetched layout).
    var custom_w: f64 = 0;
    var custom_h: f64 = 0;
    if (blank.page == null and blank.width == null and session.page_size.len != 0) {
        if (prev_custom) {
            custom_w = prev_w;
            custom_h = prev_h;
            if (custom_w > 0 and custom_h > 0) {
                const s = core.defaultBlankSizePx(custom_w, custom_h, 96.0);
                blank.width = @intCast(s.w);
                blank.height = @intCast(s.h);
            }
        } else {
            blank.page = prev_page;
        }
    }
    const img = try pipeline.acquireBlank(session.gpa, blank);
    try session.loadImage(img, "blank", true, .png, null);
    // Keep the page the blank was actually created on as the session's picked format. Explicit
    // dims size the blank but keep the previous /format pick (matching the Telegram bot, which
    // preserves its session PageFormat across an explicit-dims /blank).
    if (blank.page) |p| {
        session.setPageSize(p) catch {};
    } else if (custom_w > 0 and custom_h > 0) {
        session.setPageSize("custom") catch {};
        session.custom_page_w = custom_w;
        session.custom_page_h = custom_h;
    } else if (prev_page) |p| {
        session.setPageSize(p) catch {};
    } else if (prev_custom and prev_w > 0 and prev_h > 0) {
        session.setPageSize("custom") catch {};
        session.custom_page_w = prev_w;
        session.custom_page_h = prev_h;
    }
    ui.redraw(session);
}

/// Where a `/save` should write. Pure so the routing is unit-tested without I/O.
pub const SaveTarget = enum { local, server, none };

/// `/save <path>` writes locally; a bare `/save` pushes the result to the active server
/// project (the manual counterpart to `/sync`, usable when sync is off); a bare `/save`
/// with no active project is an error (nothing to write to).
pub fn saveTarget(arg_len: usize, has_remote: bool) SaveTarget {
    if (arg_len != 0) return .local;
    if (has_remote) return .server;
    return .none;
}

pub fn doSave(session: *Session, io: std.Io, arg: []const u8) !void {
    if (!session.hasImage()) return ui.noImage();
    switch (saveTarget(arg.len, session.hasRemote())) {
        .none => logo.err("save needs an output path — e.g. '/save out.png' (or a bare '/save' to push to the active server project)\n", .{}),
        .server => {
            // Manual server push: upload the current result now even when sync is off, so
            // there is always a way to update the server image after new edits.
            remoteEvents.pushResult(session);
            session.dirty = false; // a manual push satisfies any pending sync
        },
        .local => {
            // A `.stencil` path saves the whole project (image + layout + metadata) in one file.
            if (project.isStencilPath(arg)) return saveProject(session, io, arg);
            // The wrote line reports the page actually used — the same label the session
            // header shows (named pick oriented to the image, or "custom <w>×<h>cm").
            const page_label = try session.pageFormatLabel();
            defer session.gpa.free(page_label);
            pipeline.writeOutputLabeled(session.gpa, io, session.current().*, arg, session.default_fmt, page_label) catch return;
            // When syncing, a local save also queues a push of the result to the active project.
            remoteEvents.markDirty(session);
        },
    }
}

/// `/layout [path]` — export the current structured layout JSON (distinct from `/apply`,
/// which *draws* one). A `.json` path is exact; another path is a directory/prefix getting
/// "<path>/<project>.json"; bare writes "<project>.json" in the cwd.
pub fn doLayout(session: *Session, io: std.Io, arg: []const u8) !void {
    if (!session.hasImage()) return ui.noImage();
    const json = try session.currentLayoutJson();
    defer session.gpa.free(json);
    const name = commands.projectBaseName(session.label orelse "layout");
    const path = try commands.layoutTarget(session.gpa, arg, name);
    defer session.gpa.free(path);
    std.Io.Dir.cwd().writeFile(io, .{ .sub_path = path, .data = json }) catch |e| {
        logo.err("could not write layout to {s} ({s})\n", .{ path, @errorName(e) });
        return;
    };
    logo.print("wrote {s} (layout)\n", .{path});
}

/// `/save <file>.stencil` — bundle the ORIGINAL image + layout + metadata; prints a `/layout`-style line outside the mcp/bot `wrote`-line contract.
pub fn saveProject(session: *Session, io: std.Io, path: []const u8) !void {
    project.saveInto(session, io, path, .{
        .name = commands.projectBaseName(session.label orelse "project"),
        .color = session.remote_color orelse "",
    }) catch {}; // message already printed; the console keeps running
}

/// Which rejection (if any) blocks a `/delete <arg>` before touching disk. Pure so the guard
/// order is unit-tested without I/O — mirrors saveTarget.
pub const DeleteReject = enum { ok, empty, url, not_stencil, traversal };
pub fn deleteReject(arg: []const u8) DeleteReject {
    if (arg.len == 0) return .empty;
    if (net.isUrl(arg)) return .url; // URLs aren't local files
    if (!project.isStencilPath(arg)) return .not_stencil; // scoped to project files, not a general rm
    if (pipeline.hasParentTraversal(arg)) return .traversal; // no escaping the cwd (parity with /save)
    return .ok;
}

/// `/delete <file>.stencil` (aliases `del`/`remove`/`rm`) — delete a local `.stencil` project file
/// from disk (parity with the browser/desktop trash button). Nothing about the open session
/// changes; the guards keep the console from becoming a general file remover.
pub fn doDelete(io: std.Io, arg: []const u8) !void {
    switch (deleteReject(arg)) {
        .ok => {},
        .empty => return logo.err("delete needs a .stencil path — e.g. '/delete project.stencil'\n", .{}),
        .url => return logo.err("delete only removes local files, not URLs\n", .{}),
        .not_stencil => return logo.err("delete only removes .stencil project files (got '{s}')\n", .{arg}),
        .traversal => return logo.err("refusing to delete a path that escapes the working directory: '{s}'\n", .{arg}),
    }
    std.Io.Dir.cwd().deleteFile(io, arg) catch |e|
        return logo.err("could not delete {s} ({s})\n", .{ arg, @errorName(e) });
    logo.print("deleted {s}\n", .{arg});
}

pub fn printFormula(session: *Session) void {
    const fx = if (session.formula_x.len != 0) session.formula_x else "(identity)";
    const fy = if (session.formula_y.len != 0) session.formula_y else "(identity)";
    logo.print("formulas {s}: x -> {s}, y -> {s}\n", .{ if (session.allow_formulas) "on" else "off", fx, fy });
}

/// `/formula [x|y <expr> | on | off | clear]` — the coordinate-transform formulas riding
/// the saved layout (validated with the shared parser). Bare shows the state. Returns true
/// only when the formula state changed, so the caller only queues a sync on a real edit.
pub fn doFormula(session: *Session, arg: []const u8) bool {
    const trimmed = std.mem.trim(u8, arg, " \t");
    if (trimmed.len == 0) {
        printFormula(session);
        return false;
    }
    // Split into the sub-command word + the remainder (the expression, which may have spaces).
    var i: usize = 0;
    while (i < trimmed.len and trimmed[i] != ' ' and trimmed[i] != '\t') : (i += 1) {}
    const sub = trimmed[0..i];
    const expr = std.mem.trim(u8, trimmed[i..], " \t");
    const eq = std.ascii.eqlIgnoreCase;
    if (eq(sub, "on")) {
        session.setAllowFormulas(true);
        printFormula(session);
    } else if (eq(sub, "off")) {
        session.setAllowFormulas(false);
        logo.print("formulas off (expressions kept)\n", .{});
    } else if (eq(sub, "clear") or eq(sub, "none")) {
        session.clearFormulas();
        logo.print("formulas cleared\n", .{});
    } else if (eq(sub, "x") or eq(sub, "y")) {
        const axis: u8 = if (eq(sub, "y")) 'y' else 'x';
        const ok = session.setFormula(axis, expr) catch {
            logo.err("out of memory\n", .{});
            return false;
        };
        if (!ok) {
            logo.err("invalid {c} formula: {s}\n", .{ axis, expr });
            return false;
        }
        printFormula(session);
    } else {
        logo.print("usage: /formula [x|y <expr> | on | off | clear]   (e.g. '/formula x x*2 + 1')\n", .{});
        return false;
    }
    return true;
}

/// `/format [name | custom <w> <h>]` — show or set the session's page format (bare lists,
/// a name picks, custom sets cm dims). Drives the header label, the saved `pageSize`, and
/// the `/blank` default. Returns true only when the pick changed (a listing never syncs).
pub fn doFormat(session: *Session, arg: []const u8) bool {
    const trimmed = std.mem.trim(u8, arg, " \t");
    if (trimmed.len == 0) {
        ui.listFormats(session);
        return false;
    }

    var it = std.mem.tokenizeAny(u8, trimmed, " \t");
    const head = it.next().?;
    if (std.ascii.eqlIgnoreCase(head, "custom")) {
        const w = parseCmDim(it.next());
        const h = parseCmDim(it.next());
        if (w == null or h == null or it.next() != null) {
            logo.err("custom takes width + height in cm (0.1–500) — e.g. '/format custom 21 29.7'\n", .{});
            return false;
        }
        session.setPageSize("custom") catch return false;
        session.custom_page_w = w.?;
        session.custom_page_h = h.?;
        logo.print("page format set to custom ({d}×{d}cm)\n", .{ w.?, h.? });
        return true;
    }

    const name = core.canonicalPageFormat(head) orelse {
        logo.err("unknown page format '{s}' — type '/format' to list them\n", .{head});
        return false;
    };
    if (it.next() != null) {
        logo.err("/format takes one name — e.g. '/format b5' (or '/format custom <w> <h>')\n", .{});
        return false;
    }
    const p = core.namedPageSize(session.gpa, name) orelse return false;
    session.setPageSize(name) catch return false;
    logo.print("page format set to {s} ({d}×{d}cm)\n", .{ name, p.w, p.h });
    return true;
}

/// A `/format custom` dimension token: cm as a positive float within the shared
/// custom-page range (0.1–500 cm, mirroring the browser/desktop inputs).
fn parseCmDim(tok: ?[]const u8) ?f64 {
    const t = tok orelse return null;
    const v = std.fmt.parseFloat(f64, t) catch return null;
    if (!(v >= 0.1 and v <= 500)) return null; // also rejects NaN
    return v;
}

// ── server connections ─────────────────────────────────────────────────────────

/// `/connect <url [token][ url2 ...]>` — open one or more server connections for the
/// session; a token word after a URL authenticates against a gated server (session or
/// admin token — an admin one mints a session).
pub fn doConnect(session: *Session, io: std.Io, arg: []const u8) !void {
    if (arg.len == 0) {
        logo.err("connect needs a server URL — e.g. '/connect http://host:8090 [token]'\n", .{});
        return;
    }
    const pairs = try commands.parseConnectArgs(session.gpa, arg);
    defer session.gpa.free(pairs);
    for (pairs) |p| {
        var client = server.connect(session.gpa, io, p.url, p.token) catch |e| {
            server.printConnectError(p.url, e);
            continue;
        };
        if (session.findServer(client.base) != null) {
            logo.print("already connected to {s}\n", .{client.base});
            client.deinit();
            continue;
        }
        try session.servers.append(session.gpa, client);
        session.rememberServer(client.base) catch {}; // the pool a plan `connect` resolves against
        logo.print("connected to {s}\n", .{client.base});
    }
}

/// `/disconnect [url]` — close one connection (or the most recent when omitted).
pub fn doDisconnect(session: *Session, arg: []const u8) !void {
    if (session.servers.items.len == 0) {
        logo.err("no server connections — use '/connect <url>'\n", .{});
        return;
    }
    if (arg.len == 0) {
        const last = &session.servers.items[session.servers.items.len - 1];
        if (session.events_url != null and std.mem.eql(u8, session.events_url.?, last.base)) session.closeEvents();
        logo.print("disconnected from {s}\n", .{last.base});
        last.deinit();
        _ = session.servers.pop();
        return;
    }
    const base = try server.normalizeBase(session.gpa, arg);
    defer session.gpa.free(base);
    if (session.dropServer(base)) {
        logo.print("disconnected from {s}\n", .{base});
    } else {
        logo.print("not connected to {s}\n", .{base});
    }
}

/// `/reconnect [url]` — re-establish one connection (or every connection when omitted):
/// re-issue the auth token and, for the active project's server while syncing, revive the
/// live edit-events feed (the one socket that goes stale when the server bounces or drops).
pub fn doReconnect(session: *Session, io: std.Io, arg: []const u8) !void {
    if (session.servers.items.len == 0) {
        logo.err("no server connections — use '/connect <url>'\n", .{});
        return;
    }
    if (arg.len == 0) {
        var ok: usize = 0;
        for (0..session.servers.items.len) |i| {
            if (reconnectAt(session, io, i)) ok += 1;
        }
        logo.print("reconnected {d}/{d} server(s)\n", .{ ok, session.servers.items.len });
        return;
    }
    const base = try server.normalizeBase(session.gpa, arg);
    defer session.gpa.free(base);
    const idx = session.indexOfServer(base) orelse {
        logo.print("not connected to {s} — '/connect' first\n", .{base});
        return;
    };
    _ = reconnectAt(session, io, idx);
}

/// Reconnect the server at `i` in place: open a fresh client (new token, reusing any
/// user-supplied credential so gated servers stay reachable) and swap it for the old
/// one, reviving the events feed if this server hosts the active project. Returns success.
fn reconnectAt(session: *Session, io: std.Io, i: usize) bool {
    // Copy base + credential first — the reconnect frees the old client (and its slices).
    const base = session.gpa.dupe(u8, session.servers.items[i].base) catch return false;
    defer session.gpa.free(base);
    const cred = session.gpa.dupe(u8, session.servers.items[i].credential) catch return false;
    defer session.gpa.free(cred);
    const fresh = server.connect(session.gpa, io, base, if (cred.len != 0) cred else null) catch |e| {
        logo.err("reconnect to {s} failed ({s})\n", .{ base, @errorName(e) });
        return false;
    };
    const was_events = session.events_url != null and std.mem.eql(u8, session.events_url.?, base);
    const is_active = session.remote_url != null and std.mem.eql(u8, session.remote_url.?, base);
    session.servers.items[i].deinit();
    session.servers.items[i] = fresh;
    if (was_events or is_active) session.openEvents(&session.servers.items[i]); // feed stays open even with sync off
    logo.print("reconnected to {s}\n", .{base});
    return true;
}

/// Which credential kinds `/connections` lists.
pub const ConnFilter = enum { all, admin, session };

/// Parse the optional `/connections` argument: "" (or "all") lists everything, "admin"
/// only admin-credential connections, "session" only the rest. Null = unrecognised word,
/// which the caller answers with a usage note rather than an error.
pub fn parseConnFilter(arg: []const u8) ?ConnFilter {
    const w = std.mem.trim(u8, arg, " \t\r\n");
    if (w.len == 0 or std.ascii.eqlIgnoreCase(w, "all")) return .all;
    if (std.ascii.eqlIgnoreCase(w, "admin")) return .admin;
    if (std.ascii.eqlIgnoreCase(w, "session")) return .session;
    return null;
}

/// True when a connection of `kind` belongs in a listing filtered by `f`.
pub fn connFilterMatches(f: ConnFilter, kind: server.CredentialKind) bool {
    return switch (f) {
        .all => true,
        .admin => kind == .admin,
        .session => kind != .admin, // non-admin: a plain session token, or none at all
    };
}

/// `/connections [admin|session]` — list the connected servers, each with a live
/// reachability status (a quick GET probe per server), an `[admin]` tag when the
/// credential is an admin token, and a badge for the active project's server.
pub fn doConnections(session: *Session, arg: []const u8) void {
    const filter = parseConnFilter(arg) orelse {
        logo.err("usage: /connections [admin|session]\n", .{});
        return;
    };
    if (session.servers.items.len == 0) {
        logo.print("no server connections — use '/connect <url>'\n", .{});
        return;
    }
    var shown: usize = 0;
    for (session.servers.items) |*c| {
        if (connFilterMatches(filter, c.credential_kind)) shown += 1;
    }
    if (shown == 0) {
        logo.print("no {s} connections (of {d})\n", .{ @tagName(filter), session.servers.items.len });
        return;
    }
    logo.print("connections ({d}):\n", .{shown});
    for (session.servers.items) |*c| {
        if (!connFilterMatches(filter, c.credential_kind)) continue;
        const active = session.remote_url != null and std.mem.eql(u8, session.remote_url.?, c.base);
        logo.print("  {s}{s}  [{s}]{s}\n", .{
            c.base,
            if (c.credential_kind == .admin) "  [admin]" else "",
            probeStatus(c),
            if (active) "  (active project)" else "",
        });
    }
}

/// Probe one server's reachability for the `/connections` status column: a cheap GET that
/// distinguishes a live server from an expired token or an unreachable host.
fn probeStatus(c: *server.Client) []const u8 {
    const body = c.listProjects() catch |e| return switch (e) {
        server.Error.Unauthorized => "auth expired — /reconnect",
        else => "unreachable",
    };
    c.gpa.free(body);
    return "connected";
}

/// `/projects [url]` — list a server's projects as an aligned table (NAME / SIZE / CHANGED,
/// plus a SERVER column when listing across more than one server). With no URL it lists every
/// connected server's projects; with a URL, just that one. Open one with '/fetch <name>'.
pub fn doProjects(session: *Session, io: std.Io, arg: []const u8) !void {
    if (session.servers.items.len == 0) {
        logo.print("no server connections — use '/connect <url>'\n", .{});
        return;
    }
    const now = std.Io.Clock.real.now(io).toMilliseconds();

    var rows: std.ArrayList(projectsTable.ProjectRow) = .empty;
    defer projectsTable.freeRows(session.gpa, &rows);

    var multi = false;
    if (arg.len != 0) {
        const base = try server.normalizeBase(session.gpa, arg);
        defer session.gpa.free(base);
        const client = session.findServer(base) orelse {
            logo.print("not connected to {s} — '/connect' first\n", .{base});
            return;
        };
        try projectsTable.gatherRows(session.gpa, &rows, client, now, false);
        logo.print("projects on {s} ({d}):\n", .{ client.base, rows.items.len });
    } else {
        multi = session.servers.items.len > 1;
        for (session.servers.items) |*c| try projectsTable.gatherRows(session.gpa, &rows, c, now, multi);
        if (multi) {
            logo.print("projects across {d} servers ({d}):\n", .{ session.servers.items.len, rows.items.len });
        } else {
            logo.print("projects on {s} ({d}):\n", .{ session.servers.items[0].base, rows.items.len });
        }
    }

    if (rows.items.len == 0) {
        logo.print("  (none)\n", .{});
        return;
    }
    projectsTable.renderTable(session.gpa, rows.items, multi);
    logo.print("use '/fetch <name>' to open a project\n", .{});
}

/// The connected client + id for the active server project, or null (error printed) when
/// none is active or its server isn't connected. Shared by `/project-*`, `/rename`, `/expire`.
const ActiveProject = struct { client: *server.Client, id: []const u8 };
fn requireActiveProject(session: *Session) ?ActiveProject {
    if (!session.hasRemote()) {
        logo.err("no active server project — '/fetch <name>' first\n", .{});
        return null;
    }
    const client = session.findServer(session.remote_url.?) orelse {
        logo.err("the active project's server is not connected — '/reconnect' first\n", .{});
        return null;
    };
    return .{ .client = client, .id = session.remote_id.? };
}

/// `/project-color [#hex | name | clear]` — show or set the active server project's custom
/// name colour (empty = theme accent; `/projects` paints the name in it). A '#hex'/CSS-name
/// is validated by the core parser, normalised, and PUT; 'clear'/'none'/'default' resets.
pub fn doProjectColor(session: *Session, arg: []const u8) !void {
    const active = requireActiveProject(session) orelse return;
    const client = active.client;
    const id = active.id;
    const trimmed = std.mem.trim(u8, arg, " \t");

    // No argument: read and show the project's current colour, rendered in it.
    if (trimmed.len == 0) {
        const color = client.getProjectColor(id) catch |e| {
            logo.err("could not read the project colour ({s})\n", .{@errorName(e)});
            return;
        };
        defer session.gpa.free(color);
        printProjectColor("project colour", color);
        return;
    }

    // Otherwise resolve the new colour: a clear/reset keyword → "", else a validated #rrggbb.
    var hexbuf: [8]u8 = undefined;
    var color: []const u8 = "";
    if (!isClearWord(trimmed)) {
        const col = core.parseColor(session.gpa, trimmed) orelse {
            logo.err("invalid colour '{s}' — give a '#rrggbb' / name, or 'clear'\n", .{trimmed});
            return;
        };
        color = std.fmt.bufPrint(&hexbuf, "#{x:0>2}{x:0>2}{x:0>2}", .{ col.r, col.g, col.b }) catch "#000000";
    }

    if (!putProjectField(session, client, id, color, .color)) return; // message already printed
    session.setRemoteColor(color) catch {}; // so the status header repaints the name in it
    if (color.len == 0) {
        logo.print("project colour cleared (neutral grey)\n", .{});
    } else {
        printProjectColor("project colour set to", color);
    }
    ui.status(session); // reprint "image: <name> …" with the name in its new colour
}

/// `/blank-color [<#rrggbb>|<name>]` — get/set the active project's blank fill colour. Only a
/// blank-image project has one; setting recolours its stored blank metadata (the front-end that
/// owns the canvas regenerates the raster). No clear form — a blank always has a fill.
pub fn doProjectBlankColor(session: *Session, arg: []const u8) !void {
    const active = requireActiveProject(session) orelse return;
    const client = active.client;
    const id = active.id;
    const trimmed = std.mem.trim(u8, arg, " \t");

    // Current fill colour ("" = not a blank project).
    const cur = client.getProjectBlankColor(id) catch |e| {
        logo.err("could not read the blank colour ({s})\n", .{@errorName(e)});
        return;
    };
    defer session.gpa.free(cur);

    if (trimmed.len == 0) {
        if (cur.len == 0) {
            logo.print("blank colour: (this project is not a blank image)\n", .{});
        } else {
            printProjectColor("blank colour", cur);
        }
        return;
    }
    if (cur.len == 0) {
        logo.err("this project is not a blank image — nothing to recolour\n", .{});
        return;
    }
    var hexbuf: [8]u8 = undefined;
    const col = core.parseColor(session.gpa, trimmed) orelse {
        logo.err("invalid colour '{s}' — give a '#rrggbb' / name\n", .{trimmed});
        return;
    };
    const color = std.fmt.bufPrint(&hexbuf, "#{x:0>2}{x:0>2}{x:0>2}", .{ col.r, col.g, col.b }) catch "#000000";
    if (!putProjectField(session, client, id, color, .blank_color)) return; // message already printed
    printProjectColor("blank colour set to", color);
}

/// `/project-description [<text...>]` — set (or with no text clear) the active server
/// project's description, shown as `/projects`' trailing note. The whole argument is taken
/// verbatim; a ~2000-char soft cap guards pathological input.
pub fn doProjectDescription(session: *Session, arg: []const u8) !void {
    const active = requireActiveProject(session) orelse return;
    const client = active.client;
    const id = active.id;
    const trimmed = std.mem.trim(u8, arg, " \t");
    if (trimmed.len > 2000) {
        logo.err("description too long ({d} bytes) — keep it under 2000\n", .{trimmed.len});
        return;
    }

    if (!putProjectField(session, client, id, trimmed, .description)) return; // message already printed
    if (trimmed.len == 0) {
        logo.print("project description cleared\n", .{});
    } else {
        logo.print("project description set\n", .{});
    }
}

/// A reset keyword for `/project-color`: clears the custom colour back to the theme accent.
fn isClearWord(s: []const u8) bool {
    const eq = std.ascii.eqlIgnoreCase;
    return eq(s, "clear") or eq(s, "none") or eq(s, "default");
}

const ProjectField = enum { color, name, blank_color, description };

/// Version-guarded PUT of one project metadata field with a 409 re-read-and-retry (a peer
/// saved first), mirroring pushLayout; advances the LWW guard so our own echo isn't taken
/// for a peer edit. Returns success; prints on a hard failure.
fn putProjectField(session: *Session, client: *server.Client, id: []const u8, value: []const u8, field: ProjectField) bool {
    var tries: u8 = 0;
    while (tries < 4) : (tries += 1) {
        const res = switch (field) {
            .color => client.updateProjectColor(id, value, session.remote_version),
            .name => client.updateProjectName(id, value, session.remote_version),
            .blank_color => client.updateProjectBlankColor(id, value, session.remote_version),
            .description => client.updateProjectDescription(id, value, session.remote_version),
        };
        res catch |e| {
            if (e == server.Error.Conflict) {
                if (client.getProjectVersion(id)) |v| {
                    session.remote_version = v;
                    continue; // re-read won the race; retry the PUT
                } else |_| return false;
            }
            switch (field) {
                .color => logo.err("could not set the project colour ({s})\n", .{@errorName(e)}),
                .name => logo.err("could not rename the project ({s})\n", .{@errorName(e)}),
                .blank_color => logo.err("could not set the blank colour ({s})\n", .{@errorName(e)}),
                .description => logo.err("could not set the description ({s})\n", .{@errorName(e)}),
            }
            return false;
        };
        if (client.getProjectVersion(id)) |v| {
            session.remote_version = v;
        } else |_| {}
        return true;
    }
    return false;
}

// ── keywords (server projects, addressed by name) ─────────────────────────────

const KwMode = enum { add, del };

/// Case-insensitive substring test (std.mem has no case-insensitive `contains`).
fn containsIgnoreCase(haystack: []const u8, needle: []const u8) bool {
    if (needle.len == 0) return true;
    if (needle.len > haystack.len) return false;
    var i: usize = 0;
    while (i + needle.len <= haystack.len) : (i += 1) {
        if (std.ascii.eqlIgnoreCase(haystack[i .. i + needle.len], needle)) return true;
    }
    return false;
}

/// Print a project's keyword set (or "(none)").
fn printKeywordCsv(keywords: []const []const u8) void {
    for (keywords, 0..) |k, i| logo.print("{s} {s}", .{ if (i == 0) "" else ",", k });
}

fn printKeywords(name: []const u8, keywords: []const []const u8) void {
    if (keywords.len == 0) {
        logo.print("keywords for \"{s}\": (none)\n", .{name});
        return;
    }
    logo.print("keywords for \"{s}\":", .{name});
    printKeywordCsv(keywords);
    logo.print("\n", .{});
}

/// Split `<project | ["a","b"]> <keyword...>` into the target spec and keyword remainder (both
/// slices into `arg`). A leading '[' captures up to the matching ']'; a leading '"' a quoted
/// name; else the first whitespace token.
fn splitTargetSpec(arg: []const u8) struct { target: []const u8, rest: []const u8 } {
    const a = std.mem.trim(u8, arg, " \t");
    if (a.len == 0) return .{ .target = "", .rest = "" };
    if (a[0] == '[') {
        if (std.mem.indexOfScalar(u8, a, ']')) |end|
            return .{ .target = a[0 .. end + 1], .rest = std.mem.trim(u8, a[end + 1 ..], " \t,") };
        return .{ .target = a, .rest = "" };
    }
    if (a[0] == '"') {
        if (std.mem.indexOfScalarPos(u8, a, 1, '"')) |end|
            return .{ .target = a[1..end], .rest = std.mem.trim(u8, a[end + 1 ..], " \t,") };
        return .{ .target = a[1..], .rest = "" };
    }
    if (std.mem.indexOfAny(u8, a, " \t")) |sp|
        return .{ .target = a[0..sp], .rest = std.mem.trim(u8, a[sp + 1 ..], " \t") };
    return .{ .target = a, .rest = "" };
}

/// Collect project names from a target spec: a bracketed comma list, or a single name. Names are
/// slices into `spec`; the returned ArrayList must be deinit'd by the caller.
fn collectTargetNames(gpa: std.mem.Allocator, spec: []const u8) !std.ArrayList([]const u8) {
    var out: std.ArrayList([]const u8) = .empty;
    errdefer out.deinit(gpa);
    const s = std.mem.trim(u8, spec, " \t");
    if (s.len != 0 and s[0] == '[') {
        const inner = if (s[s.len - 1] == ']') s[1 .. s.len - 1] else s[1..];
        var it = std.mem.tokenizeScalar(u8, inner, ',');
        while (it.next()) |tok| {
            const name = std.mem.trim(u8, tok, " \t\"");
            if (name.len != 0) try out.append(gpa, name);
        }
    } else {
        const name = std.mem.trim(u8, s, "\" \t");
        if (name.len != 0) try out.append(gpa, name);
    }
    return out;
}

/// Version-guarded PUT of the keyword set with a 409 retry (a peer saved first → re-read the
/// version + retry), mirroring putProjectField.
fn putKeywords(client: *server.Client, id: []const u8, keywords: []const []const u8, version_in: i64) bool {
    var version = version_in;
    var tries: u8 = 0;
    while (tries < 4) : (tries += 1) {
        client.updateProjectKeywords(id, keywords, version) catch |e| {
            if (e == server.Error.Conflict) {
                if (client.getProjectVersion(id)) |v| {
                    version = v;
                    continue;
                } else |_| return false;
            }
            logo.err("could not update keywords ({s})\n", .{@errorName(e)});
            return false;
        };
        return true;
    }
    return false;
}

/// Resolve a project by name across connected servers, apply an add/del of `delta` keywords, PUT
/// the result version-guarded, and print the resulting set (or an error / not-found).
fn applyKeywordChange(session: *Session, name: []const u8, delta: []const []const u8, mode: KwMode) !void {
    var client: ?*server.Client = null;
    var ref: ?server.ProjectRef = null;
    for (session.servers.items) |*c| {
        const r = c.findProjectRef(name) catch |e| {
            logo.err("could not query {s} ({s})\n", .{ c.base, @errorName(e) });
            continue;
        };
        if (r) |rr| {
            client = c;
            ref = rr;
            break;
        }
    }
    if (client == null) {
        logo.print("no project named \"{s}\" on any connected server\n", .{name});
        return;
    }
    const cl = client.?;
    const rf = ref.?;
    defer session.gpa.free(rf.id);

    const current = cl.getProjectKeywords(rf.id) catch |e| {
        logo.err("could not read keywords for \"{s}\" ({s})\n", .{ name, @errorName(e) });
        return;
    };
    defer server.freeStrList(session.gpa, current);

    var next: std.ArrayList([]const u8) = .empty;
    defer next.deinit(session.gpa);
    if (mode == .add) {
        for (current) |k| try next.append(session.gpa, k);
        for (delta) |k| {
            var dup = false;
            for (next.items) |e| {
                if (std.ascii.eqlIgnoreCase(e, k)) {
                    dup = true;
                    break;
                }
            }
            if (!dup) try next.append(session.gpa, k);
        }
    } else {
        for (current) |k| {
            var drop = false;
            for (delta) |d| {
                if (std.ascii.eqlIgnoreCase(k, d)) {
                    drop = true;
                    break;
                }
            }
            if (!drop) try next.append(session.gpa, k);
        }
    }

    if (!putKeywords(cl, rf.id, next.items, rf.version)) return;
    printKeywords(name, next.items);
}

/// `/keywords <project | ["a","b"]>` — show one or more projects' keyword sets.
pub fn doKeywords(session: *Session, arg: []const u8) !void {
    if (session.servers.items.len == 0) {
        logo.print("no server connections — use '/connect <url>'\n", .{});
        return;
    }
    if (std.mem.trim(u8, arg, " \t").len == 0) {
        logo.print("usage: /keywords <project>\n", .{});
        return;
    }
    var names = try collectTargetNames(session.gpa, arg);
    defer names.deinit(session.gpa);
    for (names.items) |name| {
        var shown = false;
        for (session.servers.items) |*c| {
            const r = c.findProjectRef(name) catch continue;
            if (r) |rf| {
                defer session.gpa.free(rf.id);
                const kws = c.getProjectKeywords(rf.id) catch |e| {
                    logo.err("could not read keywords for \"{s}\" ({s})\n", .{ name, @errorName(e) });
                    shown = true;
                    break;
                };
                defer server.freeStrList(session.gpa, kws);
                printKeywords(name, kws);
                shown = true;
                break;
            }
        }
        if (!shown) logo.print("no project named \"{s}\" on any connected server\n", .{name});
    }
}

/// `/keywords-search <keyword...>` — list projects across servers whose keywords match any term
/// (case-insensitive substring).
pub fn doKeywordsSearch(session: *Session, arg: []const u8) !void {
    if (session.servers.items.len == 0) {
        logo.print("no server connections — use '/connect <url>'\n", .{});
        return;
    }
    var terms: std.ArrayList([]const u8) = .empty;
    defer terms.deinit(session.gpa);
    var tit = std.mem.tokenizeAny(u8, arg, " \t,");
    while (tit.next()) |t| try terms.append(session.gpa, t);
    if (terms.items.len == 0) {
        logo.print("usage: /keywords-search <keyword...>\n", .{});
        return;
    }
    var found: usize = 0;
    for (session.servers.items) |*c| {
        const items = c.listProjectInfos() catch |e| {
            logo.err("could not list projects on {s} ({s})\n", .{ c.base, @errorName(e) });
            continue;
        };
        defer server.freeProjectList(session.gpa, items);
        for (items) |p| {
            var hit = false;
            for (p.keywords) |kw| {
                for (terms.items) |t| {
                    if (containsIgnoreCase(kw, t)) {
                        hit = true;
                        break;
                    }
                }
                if (hit) break;
            }
            if (hit) {
                found += 1;
                logo.print("  {s}  ({s}):", .{ p.name, c.base });
                printKeywordCsv(p.keywords);
                logo.print("\n", .{});
            }
        }
    }
    if (found == 0) logo.print("no projects match those keywords\n", .{});
}

/// `/keywords-add <project | ["a","b"]> <keyword...>` — add keywords to one or more projects.
pub fn doKeywordsAdd(session: *Session, arg: []const u8) !void {
    try doKeywordsChange(session, arg, .add);
}

/// `/keywords-del <project | ["a","b"]> <keyword...>` — remove keywords from one or more projects.
pub fn doKeywordsDel(session: *Session, arg: []const u8) !void {
    try doKeywordsChange(session, arg, .del);
}

fn doKeywordsChange(session: *Session, arg: []const u8, mode: KwMode) !void {
    if (session.servers.items.len == 0) {
        logo.err("no server connections — use '/connect <url>'\n", .{});
        return;
    }
    const split = splitTargetSpec(arg);
    var names = try collectTargetNames(session.gpa, split.target);
    defer names.deinit(session.gpa);
    var delta: std.ArrayList([]const u8) = .empty;
    defer delta.deinit(session.gpa);
    var dit = std.mem.tokenizeAny(u8, split.rest, " \t,");
    while (dit.next()) |k| try delta.append(session.gpa, k);
    if (names.items.len == 0 or delta.items.len == 0) {
        const verb = if (mode == .add) "add" else "del";
        logo.print("usage: /keywords-{s} <project | [\"a\",\"b\"]> <keyword...>\n", .{verb});
        return;
    }
    for (names.items) |name| try applyKeywordChange(session, name, delta.items, mode);
}

/// `/rename <new name>` — rename the active fetched project, pushed live to the server
/// (version-guarded). Updates the displayed label + reprints the header.
pub fn doRename(session: *Session, arg: []const u8) !void {
    const active = requireActiveProject(session) orelse return;
    const name = std.mem.trim(u8, arg, " \t");
    if (name.len == 0) {
        logo.err("give a new name — e.g. '/rename MyProject'\n", .{});
        return;
    }
    if (!putProjectField(session, active.client, active.id, name, .name)) return;
    session.setLabel(name) catch {};
    logo.print("renamed to \"{s}\"\n", .{name});
    ui.status(session); // reprint "image: <name> …" with the new name
}

/// `/expire [<duration>]` — set the active server project's expiry from a core-parsed
/// duration ("days 23", "month", "off"); bare prints the accepted formats. A valid spec is
/// resolved to now+duration and PUT version-guarded; "off"/"never" clears (0 = keep forever).
pub fn doExpire(session: *Session, io: std.Io, arg: []const u8) !void {
    const spec = std.mem.trim(u8, arg, " \t");
    if (spec.len == 0) {
        printExpireFormats();
        return;
    }
    const active = requireActiveProject(session) orelse return;
    const client = active.client;
    const ms = core.parseDuration(session.gpa, spec) orelse {
        logo.err("invalid duration '{s}'\n", .{spec});
        printExpireFormats();
        return;
    };
    const now = std.Io.Clock.real.now(io).toMilliseconds();
    const expires_at: i64 = if (ms == 0) 0 else now + ms;
    if (!putProjectExpiry(session, client, active.id, expires_at)) return; // message printed
    if (expires_at == 0) {
        logo.print("expiration cleared — project kept forever\n", .{});
    } else {
        var tb: [32]u8 = undefined;
        logo.print("expires {s}\n", .{server.formatUntil(&tb, now, expires_at)});
    }
}

/// Print the `/expire` duration formats (also shown on a bad argument).
fn printExpireFormats() void {
    logo.print("usage: /expire <duration> — set when the active server project expires\n", .{});
    logo.print("  a unit alone (one of it): day | week | fortnight | month | year\n", .{});
    logo.print("  a count + unit (either order): 'days 23' | 'months 3' | '3 weeks'\n", .{});
    logo.print("  keep forever: off | never | none\n", .{});
}

/// Version-guarded PUT of a project's expiry (epoch ms; 0 = keep forever) with a 409 retry,
/// mirroring putProjectField. Advances the LWW guard on success. Prints on a hard failure.
fn putProjectExpiry(session: *Session, client: *server.Client, id: []const u8, expires_at: i64) bool {
    var tries: u8 = 0;
    while (tries < 4) : (tries += 1) {
        client.updateProjectExpiry(id, expires_at, session.remote_version) catch |e| {
            if (e == server.Error.Conflict) {
                if (client.getProjectVersion(id)) |v| {
                    session.remote_version = v;
                    continue; // re-read won the race; retry the PUT
                } else |_| return false;
            }
            logo.err("could not set the project expiry ({s})\n", .{@errorName(e)});
            return false;
        };
        if (client.getProjectVersion(id)) |v| {
            session.remote_version = v;
        } else |_| {}
        return true;
    }
    return false;
}

/// Print "<label>: <colour>" with the colour rendered in itself (truecolor) when colour is on
/// and the hex parses; an empty colour reads as "(none — neutral grey)".
fn printProjectColor(label: []const u8, color: []const u8) void {
    if (color.len == 0) {
        logo.print("{s}: (none — neutral grey)\n", .{label});
        return;
    }
    var buf: [20]u8 = undefined;
    if (logo.colorEnabled()) {
        if (theme.sgrForHex(color, &buf)) |seq| {
            logo.print("{s}: {s}{s}{s}\n", .{ label, seq, color, logo.resetSeq() });
            return;
        }
    }
    logo.print("{s}: {s}\n", .{ label, color });
}

/// `/fetch <project name> [url]` — load a server project's image to continue editing. A
/// bare `/fetch` shows what there is to fetch: the projects table, plus the usage hint.
pub fn doFetch(session: *Session, io: std.Io, arg: []const u8) !void {
    if (arg.len == 0) {
        if (session.servers.items.len == 0) {
            logo.err("no connections — '/connect <url>' first\n", .{});
            return;
        }
        try doProjects(session, io, "");
        logo.print("usage: /fetch <name> [url] — e.g. '/fetch MyProject'\n", .{});
        return;
    }
    var it = std.mem.tokenizeAny(u8, arg, " \t");
    const name = it.next().?;
    const url_arg = it.next();

    // Choose the server: a given URL, or the only connection.
    var client: *server.Client = undefined;
    if (url_arg) |u| {
        const base = try server.normalizeBase(session.gpa, u);
        defer session.gpa.free(base);
        client = session.findServer(base) orelse {
            logo.err("not connected to {s} — '/connect' first\n", .{base});
            return;
        };
    } else if (session.servers.items.len == 1) {
        client = &session.servers.items[0];
    } else if (session.servers.items.len == 0) {
        logo.err("no connections — '/connect <url>' first\n", .{});
        return;
    } else {
        logo.err("multiple servers connected — give a URL: '/fetch {s} <url>'\n", .{name});
        return;
    }

    const ref = (client.findProjectRef(name) catch |e| {
        logo.err("server lookup failed ({s})\n", .{@errorName(e)});
        return;
    }) orelse {
        logo.err("no project named \"{s}\" on {s}\n", .{ name, client.base });
        return;
    };
    defer session.gpa.free(ref.id);

    const bytes = client.downloadFile(ref.id, "original") catch |e| {
        logo.err("could not download image ({s})\n", .{@errorName(e)});
        return;
    };
    defer session.gpa.free(bytes);
    const img = image.decode(session.gpa, bytes) catch |e| {
        logo.err("could not decode server image ({s})\n", .{@errorName(e)});
        return;
    };
    try session.loadImage(img, name, true, .png, null);
    try session.setRemote(client.base, ref.id);
    session.remote_version = ref.version; // seed the LWW guard for live auto-pull
    // Adopt the project's custom name colour so the status header paints "<name>" in it.
    if (client.getProjectColor(ref.id)) |c| {
        defer session.gpa.free(c);
        session.setRemoteColor(c) catch {};
    } else |_| {
        session.setRemoteColor("") catch {};
    }
    remoteEvents.adoptServerLayout(session, client, ref.id); // show the project's stored crop/rotation/filter/lines
    remoteEvents.restoreServerChat(session, client, ref.id); // §12: restore the project's saved chat (only when /chat is on)
    // Open the live read-only events feed ALWAYS (not just when syncing) so a peer's name/colour
    // change updates the header even with sync off; sync only gates auto-pulling layout edits.
    session.openEvents(client);
    ui.redraw(session);
    logo.print("fetched \"{s}\" from {s} (sync {s})\n", .{ name, client.base, if (session.sync) "on" else "off" });
}

/// `/sync [on|off]` — when on, every edit (and save) uploads the result to the active project;
/// a bare `/sync` (no argument) toggles the current state.
pub fn doSync(session: *Session, arg: []const u8) void {
    const a = std.mem.trim(u8, arg, " \t");
    if (std.ascii.eqlIgnoreCase(a, "on") or std.ascii.eqlIgnoreCase(a, "true")) {
        session.sync = true;
    } else if (std.ascii.eqlIgnoreCase(a, "off") or std.ascii.eqlIgnoreCase(a, "false")) {
        session.sync = false;
    } else if (a.len == 0) {
        session.sync = !session.sync; // bare /sync toggles
    } else {
        logo.err("sync takes 'on', 'off', or nothing (to toggle)\n", .{});
        return;
    }
    logo.print("sync {s}\n", .{if (session.sync) "on" else "off"});
    if (session.sync and session.remote_id == null)
        logo.print("  (no active server project yet — use '/fetch <name>')\n", .{});
    // The live events feed stays open whenever a project is active (so a peer's name/colour change
    // updates the header even with sync off) — sync only gates auto-pulling layout edits. Ensure
    // it's open here in case sync was toggled before a project existed.
    if (session.events == null) {
        if (session.remote_url) |u| {
            if (session.findServer(u)) |client| session.openEvents(client);
        }
    }
}

/// `/chat [show|on|off|clear]` — opt-in per-project chat persistence (contract §12). Bare
/// shows; on/off sets whether the /prompt conversation is saved/restored with the project;
/// clear drops the turns and (§12.2) best-effort deletes the server chat file.
pub fn doChat(session: *Session, arg: []const u8) void {
    const a = std.mem.trim(u8, arg, " \t");
    const eq = std.ascii.eqlIgnoreCase;
    var turned_on = false;
    if (eq(a, "on") or eq(a, "true")) {
        session.chat_on = true;
        turned_on = true;
    } else if (eq(a, "off") or eq(a, "false")) {
        session.chat_on = false;
    } else if (eq(a, "clear")) {
        session.clearChat();
        logo.print("chat history cleared\n", .{});
        // Clearing the conversation clears the persisted server copy too (idempotent DELETE,
        // best-effort — a miss never surfaces as an error).
        if (session.chat_on and session.hasRemote()) {
            if (session.findServer(session.remote_url.?)) |client| {
                if (client.deleteFile(session.remote_id.?, "chat")) {
                    logo.print("  (server chat file deleted)\n", .{});
                } else |_| {}
            }
        }
        return;
    } else if (a.len != 0 and !eq(a, "show")) {
        logo.err("chat takes 'on', 'off', 'show', or 'clear'\n", .{});
        return;
    }
    logo.print("chat {s} ({d} saved turns)\n", .{ if (session.chat_on) "on" else "off", session.chat_history.items.len });
    // §12.2: say who can read a saved chat BEFORE one is written anywhere.
    if (turned_on) {
        logo.print("  saved into the .stencil project on /save; on a server project, readable by everyone it is shared with\n", .{});
    }
}

// ── transforms (crop / rotate / filter / layout, all undoable) ─────────────────
//
// Each transform updates the session's STRUCTURED edit state and rebuilds the derived view,
// so the exact edit serializes to a browser-compatible layout and shows live in GUI editors.

/// Map a /filter argument ("bw"|"sepia"|"invert"|"contour"|"none"|<colour>) onto the layout
/// filter and apply it. The named modes are checked before the colour fallback. Returns
/// false for an unrecognized argument (the caller reports the error).
pub fn applyFilterArg(session: *Session, arg: []const u8) bool {
    if (std.ascii.eqlIgnoreCase(arg, "bw")) {
        session.setFilter("bw", "") catch {};
    } else if (std.ascii.eqlIgnoreCase(arg, "sepia")) {
        session.setFilter("sepia", "") catch {};
    } else if (std.ascii.eqlIgnoreCase(arg, "invert")) {
        session.setFilter("invert", "") catch {};
    } else if (std.ascii.eqlIgnoreCase(arg, "contour")) {
        session.setFilter("contour", "") catch {};
    } else if (std.ascii.eqlIgnoreCase(arg, "none")) {
        session.setFilter("none", "") catch {};
    } else if (core.parseColor(session.gpa, arg)) |col| {
        var buf: [8]u8 = undefined;
        const hex = std.fmt.bufPrint(&buf, "#{x:0>2}{x:0>2}{x:0>2}", .{ col.r, col.g, col.b }) catch "#000000";
        session.setFilter("custom", hex) catch {};
    } else {
        return false;
    }
    return true;
}

/// `/exec <action> <args>` — run a transform by name; a bare `/exec` lists the action words.
/// Returns true when the action really edited the image (see runAction).
pub fn doExec(session: *Session, io: std.Io, arg: []const u8) bool {
    if (std.mem.trim(u8, arg, " \t").len == 0) {
        logo.print("usage: /exec <action> <args> — actions: crop | rotate | filter | apply (e.g. '/exec rotate 1')\n", .{});
        return false;
    }
    return runAction(session, io, commands.parseAction(arg));
}

/// Run one transform. Returns true only when a new edit state was actually recorded — the
/// usage/listing, no-image, bad-argument and full-turn paths mutate nothing, so the caller
/// never queues a sync upload for them.
pub fn runAction(session: *Session, io: std.Io, action: Action) bool {
    // A bare transform lists its variants / usage instead of acting (no image needed) — so
    // `/crop` never silently records a full-image crop and `/filter` shows what it takes.
    if (action.arg.len == 0) switch (action.kind) {
        .crop => {
            logo.print("usage: /crop <spec> [album] — edges x1= x2= y1= y2= with %, px, cm/mm/in, or a bare pixel delta; omit an edge to keep the image bound\n", .{});
            logo.print("       e.g. '/crop x1=10% x2=90% y1=10% y2=90%' (add 'album' to derive a missing axis from the page, landscape)\n", .{});
            return false;
        },
        .rotate => {
            logo.print("usage: /rotate <int> — quarter-turns: 1 = 90° cw, 2 = 180°, -1 = 90° ccw, 3 = 270° (e.g. '/rotate -1')\n", .{});
            return false;
        },
        .filter => {
            ui.listFilters();
            return false;
        },
        .layout => {
            logo.err("apply needs a path or URL to a layout JSON — e.g. '/apply notes.json'\n", .{});
            return false;
        },
    };
    if (!session.hasImage()) {
        ui.noImage();
        return false;
    }
    switch (action.kind) {
        .crop => {
            var album = false;
            const spec = commands.stripAlbum(session.gpa, action.arg, &album) catch return false;
            defer session.gpa.free(spec);
            const cur = session.current();
            const rect = pipeline.resolveCropSpec(session.gpa, cur.width, cur.height, spec, album) orelse return false;
            session.applyCrop(rect) catch return false;
            ui.ack(session, "cropped");
        },
        .rotate => {
            const n = std.fmt.parseInt(i32, action.arg, 10) catch {
                logo.err("rotate needs an integer (quarter-turns), e.g. '/rotate -1'\n", .{});
                return false;
            };
            if (@mod(n, 4) == 0) {
                logo.print("rotate {d} is a full turn — no change\n", .{n});
                return false;
            }
            session.applyRotate(n) catch return false;
            ui.ack(session, "rotated");
        },
        .filter => {
            if (!applyFilterArg(session, action.arg)) {
                logo.err("unknown filter \"{s}\" — 'bw', 'sepia', 'invert', 'contour', 'none', or a colour\n", .{action.arg});
                return false;
            }
            ui.ack(session, action.arg);
        },
        .layout => {
            // `apply <src> [combine|replace]` — combine (append) stays the default; the
            // GUI editors offer the same choice as a Combine/Replace prompt.
            var src = std.mem.trim(u8, action.arg, " \t");
            var replace = false;
            if (std.mem.lastIndexOfScalar(u8, src, ' ')) |i| {
                const tail = std.mem.trim(u8, src[i + 1 ..], " \t");
                if (std.ascii.eqlIgnoreCase(tail, "replace") or std.ascii.eqlIgnoreCase(tail, "combine")) {
                    replace = std.ascii.eqlIgnoreCase(tail, "replace");
                    src = std.mem.trim(u8, src[0..i], " \t");
                }
            }
            const bytes = pipeline.loadLayoutBytes(session.gpa, io, src) catch return false; // msg printed
            defer session.gpa.free(bytes);
            if (replace) {
                session.setLines(bytes) catch return false;
            } else {
                session.addLines(bytes) catch return false;
            }
            // Adopt the layout file's embedded filter, if any (layout.zig "imageFilter"/legacy "filter").
            var L = layout_mod.parse(session.gpa, bytes) catch {
                ui.ack(session, if (replace) "drawn (replaced)" else "drawn");
                return true; // the lines were added even if the filter parse failed
            };
            defer L.deinit();
            if (L.filter) |f| _ = applyFilterArg(session, f);
            ui.ack(session, if (replace) "drawn (replaced)" else "drawn");
        },
    }
    return true;
}

// ── history / clipboard / theme / session ──────────────────────────────────────

pub fn doStep(session: *Session, moved: bool, ok: []const u8, none: []const u8) void {
    if (!session.hasImage()) return ui.noImage();
    if (moved) ui.ack(session, ok) else logo.print("{s}\n", .{none});
}

pub fn doReset(session: *Session) void {
    if (!session.hasImage()) return ui.noImage();
    session.revert();
    ui.redraw(session);
    logo.print("reset to original\n", .{});
}

pub fn doDrop(session: *Session) void {
    if (!session.hasImage()) return ui.noImage();
    session.clearAll();
    ui.redraw(session); // header now reads "(none)"
}

pub fn doTheme(session: *Session, arg: []const u8) void {
    if (arg.len == 0) return ui.listThemes();

    // A named preset, with 'default' as an alias for the default accent (violet).
    const key = if (std.ascii.eqlIgnoreCase(arg, "default")) theme.default_key else arg;
    if (theme.find(key)) |a| {
        applyAccent(session, a.rgb, a.key, a.hex, true);
        return;
    }

    // Otherwise accept any colour the core understands — a '#rrggbb' hex or a CSS name.
    if (core.parseColor(session.gpa, arg)) |c| {
        var hexbuf: [8]u8 = undefined;
        const hex = std.fmt.bufPrint(&hexbuf, "#{x:0>2}{x:0>2}{x:0>2}", .{ c.r, c.g, c.b }) catch "#??????";
        applyAccent(session, .{ c.r, c.g, c.b }, hex, hex, true);
        return;
    }

    logo.err("unknown theme '{s}' — type '/theme' to list them, or give a colour like #ff5623\n", .{arg});
}

// Repaint everything in a new accent: the logo's RGB, the stored label and the screen. When
// `announce` is set it also prints a "theme set to …" line — the typed `/theme` command does,
// but logo clicks stay silent (the recoloured logo is feedback enough).
fn applyAccent(session: *Session, rgb: [3]u8, label: []const u8, hex: []const u8, announce: bool) void {
    logo.setAccent(rgb);
    ui.setAccent(label);
    // In full-screen mode recapture the pinned logo header in the new accent (keeping the
    // scrollback); otherwise fall back to the classic clear-and-reprint.
    if (screen.current()) |s| s.onThemeChanged() else ui.redraw(session);
    if (announce) logo.print("theme set to {s} ({s})\n", .{ label, hex });
}

/// Advance to the next accent preset (wrapping), or reset to the default when a custom colour
/// is active — the single-click-on-logo behaviour, mirroring the browser (accents.js). Silent.
pub fn cycleTheme(session: *Session) void {
    const key = screen.nextAccentKey(ui.currentAccentKey());
    const a = theme.find(key) orelse theme.accents()[0];
    applyAccent(session, a.rgb, a.key, a.hex, false);
}

/// Set a random vivid custom colour (outside the preset list) — the double-click-on-logo
/// behaviour, the terminal stand-in for the browser logo's colour picker. Silent. `seed` varies
/// per click (the caller passes the click time) so each double-click yields a different hue.
pub fn randomCustomTheme(session: *Session, seed: u64) void {
    var prng = std.Random.DefaultPrng.init(seed);
    const h = @as(f64, @floatFromInt(prng.random().intRangeLessThan(u16, 0, 360)));
    const rgb = theme.hsvToRgb(h, 0.7, 0.95); // always vivid + readable, essentially never a preset
    var hexbuf: [8]u8 = undefined;
    const hex = std.fmt.bufPrint(&hexbuf, "#{x:0>2}{x:0>2}{x:0>2}", .{ rgb[0], rgb[1], rgb[2] }) catch "#??????";
    applyAccent(session, rgb, hex, hex, false);
}

/// `/mouse [on|off]` (bare toggles) — mouse reporting in full-screen mode: OFF hands the
/// mouse back for native select/copy, ON re-enables logo clicks + wheel + drag-selection.
/// No-op (with a note) outside full-screen.
pub fn doMouse(session: *Session, arg: []const u8) void {
    _ = session;
    const s = screen.current() orelse {
        logo.print("mouse control is only available in --console-full-screen\n", .{});
        return;
    };
    const on = if (arg.len == 0)
        !s.mouseOn()
    else if (std.ascii.eqlIgnoreCase(arg, "on"))
        true
    else if (std.ascii.eqlIgnoreCase(arg, "off"))
        false
    else {
        logo.print("usage: /mouse [on|off] (bare toggles) — off lets you select text\n", .{});
        return;
    };
    s.setMouse(on);
    if (on)
        logo.print("mouse on — click the logo to theme, drag to select (Ctrl-S copies), wheel to scroll\n", .{})
    else
        logo.print("mouse off — you can select/copy text natively now; '/mouse on' to re-enable clicks\n", .{});
}

/// `/reveal-speed [speed]` — how fast new output reveals, 0.01 … 1 (bare shows; 1 =
/// instant; `off`/`on` name the two ends; STENCIL_CONSOLE_REVEAL_SPEED is the per-session
/// default). No-op with a note outside full-screen, the only mode painting its own output.
pub fn doRevealSpeed(session: *Session, arg: []const u8) void {
    _ = session;
    const s = screen.current() orelse {
        logo.print("reveal speed is only available in --console-full-screen\n", .{});
        return;
    };
    if (arg.len == 0) {
        report(s.revealSpeed(), true);
        return;
    }
    const want: f64 = if (std.ascii.eqlIgnoreCase(arg, "off"))
        screen.speed_max
    else if (std.ascii.eqlIgnoreCase(arg, "on"))
        screen.speed_default
    else
        screen.parseRevealSpeed(arg) orelse {
            logo.print("usage: /reveal-speed [{d} … {d}] — {d} slowest, {d} instant (default {d})\n", .{ screen.speed_min, screen.speed_max, screen.speed_min, screen.speed_max, screen.speed_default });
            return;
        };
    s.setRevealSpeed(want);
    report(s.revealSpeed(), false);
}

/// One line describing the current reveal speed — what `/reveal-speed` prints, whether it was
/// asked to show the setting or to change it. Deliberately terse: the line is itself revealed
/// at the speed it names, so it demonstrates the setting rather than describing it.
fn report(speed: f64, showing: bool) void {
    const verb = if (showing) "reveal speed is" else "reveal speed";
    if (speed >= screen.speed_max) {
        logo.print("{s} {d} — output appears instantly\n", .{ verb, speed });
        return;
    }
    logo.print("{s} {d}\n", .{ verb, speed });
}

// ── tests (pure routing / debounce logic) ──────────────────────────────────────

const testing = std.testing;

test "parseSourceUpload: positional grammar, -1 = unset, junk rejected" {
    const o = parseSourceUpload("https://x/ 2 png 100 -1 50 800").?;
    try testing.expectEqualStrings("https://x/", o.url);
    try testing.expectEqual(@as(u32, 2), o.index);
    try testing.expectEqualStrings("png", o.format);
    try testing.expectEqual(@as(u32, 100), o.min_width.?);
    try testing.expect(o.max_width == null); // -1 → unset
    try testing.expectEqual(@as(u32, 50), o.min_height.?);
    try testing.expectEqual(@as(u32, 800), o.max_height.?);

    // Bare URL → defaults (index 0, all formats, all bounds unset).
    const d = parseSourceUpload("https://x/").?;
    try testing.expectEqual(@as(u32, 0), d.index);
    try testing.expectEqualStrings("all", d.format);
    try testing.expect(d.min_width == null and d.max_height == null);

    try testing.expect(parseSourceUpload("") == null); // no url
    try testing.expect(parseSourceUpload("https://x/ notanumber") == null); // bad index
}

test "saveTarget routes a path to local, a bare save to the active project, else none" {
    // An explicit path always saves locally, regardless of an active remote.
    try testing.expectEqual(SaveTarget.local, saveTarget("out.png".len, false));
    try testing.expectEqual(SaveTarget.local, saveTarget("out.png".len, true));
    // A bare /save pushes to the active server project when one is set.
    try testing.expectEqual(SaveTarget.server, saveTarget(0, true));
    // A bare /save with nothing to write to is an error.
    try testing.expectEqual(SaveTarget.none, saveTarget(0, false));
}

test "deleteReject: empty/url/non-stencil/traversal guards gate a local .stencil delete" {
    try testing.expectEqual(DeleteReject.empty, deleteReject(""));
    try testing.expectEqual(DeleteReject.url, deleteReject("https://x/a.stencil"));
    try testing.expectEqual(DeleteReject.not_stencil, deleteReject("notes.txt"));
    try testing.expectEqual(DeleteReject.traversal, deleteReject("../up.stencil"));
    try testing.expectEqual(DeleteReject.traversal, deleteReject("sub/../../up.stencil"));
    try testing.expectEqual(DeleteReject.ok, deleteReject("project.stencil"));
    try testing.expectEqual(DeleteReject.ok, deleteReject("sub/dir/project.stencil"));
}


test "parseConnFilter: blank/all lists everything, admin+session filter, junk = usage" {
    try testing.expectEqual(ConnFilter.all, parseConnFilter("").?);
    try testing.expectEqual(ConnFilter.all, parseConnFilter("  ").?);
    try testing.expectEqual(ConnFilter.all, parseConnFilter("ALL").?);
    try testing.expectEqual(ConnFilter.admin, parseConnFilter("admin").?);
    try testing.expectEqual(ConnFilter.session, parseConnFilter(" Session ").?);
    try testing.expect(parseConnFilter("bogus") == null);
    try testing.expect(parseConnFilter("adminx") == null);

    // "session" means non-admin, which includes an anonymously-minted connection.
    try testing.expect(connFilterMatches(.all, .none) and connFilterMatches(.all, .admin));
    try testing.expect(connFilterMatches(.admin, .admin));
    try testing.expect(!connFilterMatches(.admin, .session) and !connFilterMatches(.admin, .none));
    try testing.expect(connFilterMatches(.session, .session) and connFilterMatches(.session, .none));
    try testing.expect(!connFilterMatches(.session, .admin));
}

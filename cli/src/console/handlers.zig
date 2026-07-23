//! Console command implementations: one handler per verb/transform. Each drives the same
//! pipeline.zig building blocks the flag mode uses, snapshots the result into the session's
//! undo history, and reports via ui.zig. Pure parsing lives in commands.zig; this file is
//! the I/O-bearing half (load/save/clipboard/theme + the undoable transforms).
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
const layout_mod = @import("../layout.zig");
const project = @import("../project.zig");
const ui = @import("ui.zig");
const screen = @import("screen.zig");
const Session = @import("session.zig").Session;
const Action = commands.Action;

// ── source / save ─────────────────────────────────────────────────────────────

pub fn doUpload(session: *Session, io: std.Io, arg: []const u8) !void {
    if (arg.len == 0) {
        logo.print("error: upload needs a path or URL — e.g. '/upload photo.png'\n", .{});
        return;
    }
    // A whole .stencil project loads its image + layout (crop/rotation/filter/lines) at once.
    if (project.isStencilPath(arg)) return openProject(session, io, arg);
    const src = pipeline.acquireInput(session.gpa, io, arg, 0) catch return; // message already printed
    try session.loadImage(src.img, arg, net.isUrl(arg), src.default_fmt, src.bytes);
    ui.redraw(session);
}

/// `/upload <file>.stencil` — load a portable project: decode its embedded ORIGINAL image and
/// adopt its layout (crop/rotation/filter/lines) so the view matches the browser/desktop editors.
fn openProject(session: *Session, io: std.Io, path: []const u8) !void {
    var proj = project.loadInto(session, io, path) catch return; // message already printed
    proj.deinit();
    ui.redraw(session);
}

/// `/source-upload <url> [index=0] [format=all] [minW=-1] [maxW=-1] [minH=-1] [maxH=-1]`
/// (alias `/scrape`) — scrape a page, filter to image-category items by format + dimension,
/// pick the item at 0-based `index`, and load it as the working image. `-1` = unset bound;
/// `format` `all` = any. Mirrors doUpload (ends in session.loadImage + ui.redraw).
pub fn doSourceUpload(session: *Session, io: std.Io, arg: []const u8) !void {
    if (arg.len == 0) {
        logo.print("error: source-upload needs a URL — e.g. '/source-upload https://example.com'\n", .{});
        return;
    }
    const o = parseSourceUpload(arg) orelse {
        logo.print("error: source-upload takes '<url> [index=0] [format=all] [minW=-1] [maxW=-1] [minH=-1] [maxH=-1]'\n", .{});
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
        logo.print("error: blank takes '[format] [w h] [color]' (a page format and explicit dims are exclusive) — e.g. '/blank 800 600 white' or '/blank b5 pink'\n", .{});
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
        .none => logo.print("error: save needs an output path — e.g. '/save out.png' (or a bare '/save' to push to the active server project)\n", .{}),
        .server => {
            // Manual server push: upload the current result now even when sync is off, so
            // there is always a way to update the server image after new edits.
            pushResult(session);
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
            markDirty(session);
        },
    }
}

/// `/layout [path]` — export the current structured layout JSON to a local file (distinct
/// from `/apply`, which *draws* a layout onto the image). With a `.json` path it writes there
/// exactly; a non-`.json` path is a directory/prefix and gets "<path>/<project>.json"; a bare
/// `/layout` writes "<project>.json" in the cwd (project = the working image's base name).
pub fn doLayout(session: *Session, io: std.Io, arg: []const u8) !void {
    if (!session.hasImage()) return ui.noImage();
    const json = try session.currentLayoutJson();
    defer session.gpa.free(json);
    const name = commands.projectBaseName(session.label orelse "layout");
    const path = try commands.layoutTarget(session.gpa, arg, name);
    defer session.gpa.free(path);
    std.Io.Dir.cwd().writeFile(io, .{ .sub_path = path, .data = json }) catch |e| {
        logo.print("error: could not write layout to {s} ({s})\n", .{ path, @errorName(e) });
        return;
    };
    logo.print("wrote {s} (layout)\n", .{path});
}

/// `/save <file>.stencil` — bundle the ORIGINAL image + layout + metadata; prints a `/layout`-style line outside the mcp/bot `wrote`-line contract.
fn saveProject(session: *Session, io: std.Io, path: []const u8) !void {
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
        .empty => return logo.print("error: delete needs a .stencil path — e.g. '/delete project.stencil'\n", .{}),
        .url => return logo.print("error: delete only removes local files, not URLs\n", .{}),
        .not_stencil => return logo.print("error: delete only removes .stencil project files (got '{s}')\n", .{arg}),
        .traversal => return logo.print("error: refusing to delete a path that escapes the working directory: '{s}'\n", .{arg}),
    }
    std.Io.Dir.cwd().deleteFile(io, arg) catch |e|
        return logo.print("error: could not delete {s} ({s})\n", .{ arg, @errorName(e) });
    logo.print("deleted {s}\n", .{arg});
}

fn printFormula(session: *Session) void {
    const fx = if (session.formula_x.len != 0) session.formula_x else "(identity)";
    const fy = if (session.formula_y.len != 0) session.formula_y else "(identity)";
    logo.print("formulas {s}: x -> {s}, y -> {s}\n", .{ if (session.allow_formulas) "on" else "off", fx, fy });
}

/// `/formula [x|y <expr> | on | off | clear]` — the x/y coordinate-transform formulas that
/// ride the saved layout (validated with the shared parser; the browser applies them, the CLI
/// preserves + round-trips). Bare `/formula` shows the current state. Returns true when the
/// formula state actually changed (so the caller only queues a sync on a real edit).
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
            logo.print("error: out of memory\n", .{});
            return false;
        };
        if (!ok) {
            logo.print("error: invalid {c} formula: {s}\n", .{ axis, expr });
            return false;
        }
        printFormula(session);
    } else {
        logo.print("usage: /formula [x|y <expr> | on | off | clear]   (e.g. '/formula x x*2 + 1')\n", .{});
        return false;
    }
    return true;
}

/// `/format [name | custom <w> <h>]` — show or set the session's page format. Bare lists
/// every named format with its cm size (current marked); a name (case-insensitive) picks it;
/// `custom <w> <h>` sets explicit cm dims. The pick drives the header label, the layout
/// `pageSize` written on save/sync, and the `/blank` default page. Returns true when the
/// pick actually changed (so a bare listing / rejected name never queues a sync).
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
            logo.print("error: custom takes width + height in cm (0.1–500) — e.g. '/format custom 21 29.7'\n", .{});
            return false;
        }
        session.setPageSize("custom") catch return false;
        session.custom_page_w = w.?;
        session.custom_page_h = h.?;
        logo.print("page format set to custom ({d}×{d}cm)\n", .{ w.?, h.? });
        return true;
    }

    const name = core.canonicalPageFormat(head) orelse {
        logo.print("error: unknown page format '{s}' — type '/format' to list them\n", .{head});
        return false;
    };
    if (it.next() != null) {
        logo.print("error: /format takes one name — e.g. '/format b5' (or '/format custom <w> <h>')\n", .{});
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

/// `/connect <url[ url2 ...]>` — open one or more server connections for the session.
pub fn doConnect(session: *Session, io: std.Io, arg: []const u8) !void {
    if (arg.len == 0) {
        logo.print("error: connect needs a server URL — e.g. '/connect http://host:8090'\n", .{});
        return;
    }
    var it = std.mem.tokenizeAny(u8, arg, " ,\t");
    while (it.next()) |url| {
        var client = server.connect(session.gpa, io, url, null) catch |e| {
            logo.print("error: could not connect to {s} ({s})\n", .{ url, @errorName(e) });
            continue;
        };
        if (session.findServer(client.base) != null) {
            logo.print("already connected to {s}\n", .{client.base});
            client.deinit();
            continue;
        }
        try session.servers.append(session.gpa, client);
        logo.print("connected to {s}\n", .{client.base});
    }
}

/// `/disconnect [url]` — close one connection (or the most recent when omitted).
pub fn doDisconnect(session: *Session, arg: []const u8) !void {
    if (session.servers.items.len == 0) {
        logo.print("no server connections\n", .{});
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
        logo.print("no server connections — use '/connect <url>'\n", .{});
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

/// Reconnect the server at `i` in place: open a fresh client (new token) and swap it for the
/// old one, reviving the events feed if this server hosts the active project. Returns success.
fn reconnectAt(session: *Session, io: std.Io, i: usize) bool {
    // Copy the base first — the reconnect frees the old client (and its base slice).
    const base = session.gpa.dupe(u8, session.servers.items[i].base) catch return false;
    defer session.gpa.free(base);
    const fresh = server.connect(session.gpa, io, base, null) catch |e| {
        logo.print("error: reconnect to {s} failed ({s})\n", .{ base, @errorName(e) });
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

/// `/connections` — list the connected servers, each with a live reachability status
/// (a quick GET probe per server) and a marker for the active project's server.
pub fn doConnections(session: *Session) void {
    if (session.servers.items.len == 0) {
        logo.print("no server connections — use '/connect <url>'\n", .{});
        return;
    }
    logo.print("connections ({d}):\n", .{session.servers.items.len});
    for (session.servers.items) |*c| {
        const active = session.remote_url != null and std.mem.eql(u8, session.remote_url.?, c.base);
        logo.print("  {s}  [{s}]{s}\n", .{ c.base, probeStatus(c), if (active) "  (active project)" else "" });
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

    var rows: std.ArrayList(ProjectRow) = .empty;
    defer freeRows(session.gpa, &rows);

    var multi = false;
    if (arg.len != 0) {
        const base = try server.normalizeBase(session.gpa, arg);
        defer session.gpa.free(base);
        const client = session.findServer(base) orelse {
            logo.print("not connected to {s} — '/connect' first\n", .{base});
            return;
        };
        try gatherRows(session.gpa, &rows, client, now, false);
        logo.print("projects on {s} ({d}):\n", .{ client.base, rows.items.len });
    } else {
        multi = session.servers.items.len > 1;
        for (session.servers.items) |*c| try gatherRows(session.gpa, &rows, c, now, multi);
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
    renderTable(session.gpa, rows.items, multi);
    logo.print("use '/fetch <name>' to open a project\n", .{});
}

/// One rendered project row; all fields owned so rows outlive the per-server lists they came
/// from. `color` is the project's custom name colour ("" = none → paint in the theme accent);
/// `description` is the free-text caption ("" = none → shown as a trailing dimmed note).
const ProjectRow = struct { name: []u8, size: []u8, created: []u8, expires: []u8, changed: []u8, color: []u8, description: []u8, server: []const u8 };

fn freeRows(gpa: std.mem.Allocator, rows: *std.ArrayList(ProjectRow)) void {
    for (rows.items) |r| {
        gpa.free(r.name);
        gpa.free(r.size);
        gpa.free(r.created);
        gpa.free(r.expires);
        gpa.free(r.changed);
        gpa.free(r.color);
        gpa.free(r.description);
    }
    rows.deinit(gpa);
}

/// Fetch one server's projects and append a rendered row per project. A network/listing failure
/// is reported and skipped (other servers still list); only allocation errors propagate.
fn gatherRows(gpa: std.mem.Allocator, rows: *std.ArrayList(ProjectRow), client: *server.Client, now: i64, multi: bool) !void {
    const items = client.listProjectInfos() catch |e| {
        logo.print("error: could not list projects on {s} ({s})\n", .{ client.base, @errorName(e) });
        return;
    };
    defer server.freeProjectList(gpa, items);
    for (items) |p| {
        const name = try gpa.dupe(u8, p.name);
        errdefer gpa.free(name);
        // Some projects have no stored dimensions (e.g. never rendered) — show "-", not "0x0".
        const size = if (p.w == 0 and p.h == 0) try gpa.dupe(u8, "-") else try std.fmt.allocPrint(gpa, "{d}x{d}", .{ p.w, p.h });
        errdefer gpa.free(size);
        var tb: [32]u8 = undefined;
        const changed = try gpa.dupe(u8, server.formatAgo(&tb, now, p.updated_at));
        errdefer gpa.free(changed);
        // Created date, shown relatively like CHANGED (reuses tb after `changed`
        // is already its own allocation).
        const created = try gpa.dupe(u8, server.formatAgo(&tb, now, p.created_at));
        errdefer gpa.free(created);
        // Expiry, shown forward-looking ("in 3d" / "expired" / "never"), next to CREATED.
        const expires = try gpa.dupe(u8, server.formatUntil(&tb, now, p.expires_at));
        errdefer gpa.free(expires);
        const color = try gpa.dupe(u8, p.color);
        errdefer gpa.free(color);
        const description = try gpa.dupe(u8, p.description);
        errdefer gpa.free(description);
        try rows.append(gpa, .{ .name = name, .size = size, .created = created, .expires = expires, .changed = changed, .color = color, .description = description, .server = if (multi) client.base else "" });
    }
}

/// Render the gathered rows as a left-aligned columnar table (2-space indent, 2-space gaps).
fn renderTable(gpa: std.mem.Allocator, rows: []const ProjectRow, multi: bool) void {
    var nw: usize = "NAME".len;
    var sw: usize = "SIZE".len;
    var crw: usize = "CREATED".len;
    var erw: usize = "EXPIRES".len;
    var cw: usize = "CHANGED".len;
    for (rows) |r| {
        nw = @max(nw, r.name.len);
        sw = @max(sw, r.size.len);
        crw = @max(crw, r.created.len);
        erw = @max(erw, r.expires.len);
        cw = @max(cw, r.changed.len);
    }
    printRow(gpa, "NAME", "", nw, "SIZE", sw, "CREATED", crw, "EXPIRES", erw, "CHANGED", cw, if (multi) "SERVER" else null, ""); // header: no colour/description
    for (rows) |r| {
        var buf: [20]u8 = undefined;
        printRow(gpa, r.name, theme.nameSeq(r.color, &buf), nw, r.size, sw, r.created, crw, r.expires, erw, r.changed, cw, if (multi) r.server else null, r.description);
    }
}

/// Print one table row, padding each non-final column to its width. `name_seq` colours the NAME
/// column ("" = plain); a non-empty `desc` is appended as a trailing dimmed note (truncated).
/// Best-effort.
fn printRow(gpa: std.mem.Allocator, name: []const u8, name_seq: []const u8, nw: usize, size: []const u8, sw: usize, created: []const u8, crw: usize, expires: []const u8, erw: usize, changed: []const u8, cw: usize, srv: ?[]const u8, desc: []const u8) void {
    var line: std.ArrayList(u8) = .empty;
    defer line.deinit(gpa);
    appendCol(gpa, &line, "  ", 0); // 2-space indent (no padding)
    appendName(gpa, &line, name, name_seq, nw);
    appendCol(gpa, &line, size, sw);
    appendCol(gpa, &line, created, crw);
    appendCol(gpa, &line, expires, erw);
    if (srv) |s| {
        appendCol(gpa, &line, changed, cw);
        appendCol(gpa, &line, s, 0); // final column, no trailing pad
    } else {
        appendCol(gpa, &line, changed, 0); // final column
    }
    appendDesc(gpa, &line, desc); // trailing "— <caption>" note, when present
    logo.print("{s}\n", .{line.items});
}

/// Append a free-text description as a trailing dimmed "— <caption>" note after the last column,
/// truncated to ~48 bytes (with an ellipsis) so a long caption can't blow up the row. Truncation
/// backs off any partial UTF-8 codepoint so we never emit an invalid byte. No-op when empty.
/// Best-effort.
fn appendDesc(gpa: std.mem.Allocator, line: *std.ArrayList(u8), desc: []const u8) void {
    if (desc.len == 0) return;
    const on = logo.colorEnabled();
    line.appendSlice(gpa, "  ") catch return;
    if (on) line.appendSlice(gpa, "\x1b[2m") catch {}; // faint
    line.appendSlice(gpa, "— ") catch return;
    if (desc.len > 48) {
        // Back off from byte 48 to a codepoint boundary (skip UTF-8 continuation bytes 0b10xxxxxx).
        var end: usize = 48;
        while (end > 0 and (desc[end] & 0xC0) == 0x80) : (end -= 1) {}
        line.appendSlice(gpa, desc[0..end]) catch return;
        line.appendSlice(gpa, "…") catch {};
    } else line.appendSlice(gpa, desc) catch return;
    if (on) line.appendSlice(gpa, logo.resetSeq()) catch {};
}

/// Append `text`, then (when width > 0) pad with spaces to `width` plus a 2-space column gap.
/// The final column passes width 0 to skip trailing padding. Best-effort.
fn appendCol(gpa: std.mem.Allocator, line: *std.ArrayList(u8), text: []const u8, width: usize) void {
    line.appendSlice(gpa, text) catch return;
    if (width == 0) return;
    var i = text.len;
    while (i < width + 2) : (i += 1) line.append(gpa, ' ') catch return;
}

/// Like appendCol for the NAME column, wrapping the (visible) name in `seq`…reset when a colour
/// is given. Padding is computed from the VISIBLE name length — the SGR escapes have zero
/// display width — so the columns stay aligned. Best-effort.
fn appendName(gpa: std.mem.Allocator, line: *std.ArrayList(u8), name: []const u8, seq: []const u8, width: usize) void {
    const on = seq.len != 0;
    if (on) line.appendSlice(gpa, seq) catch {};
    line.appendSlice(gpa, name) catch return;
    if (on) line.appendSlice(gpa, logo.resetSeq()) catch {};
    var i = name.len;
    while (i < width + 2) : (i += 1) line.append(gpa, ' ') catch return;
}

/// `/project-color [#hex | name | clear]` — show or set the active server project's custom
/// name colour (the colour `/projects` paints its name in; empty = the theme accent). With no
/// argument it prints the current colour rendered in that colour; a '#hex'/CSS-name is validated
/// via the core colour parser, normalised to "#rrggbb", and PUT to the server; 'clear'/'none'/
/// 'default' resets it to "" (theme fallback).
/// The connected client + id for the active server project, or null (with an error already
/// printed) when there is no active project or its server isn't connected. Shared preamble for
/// the `/project-*`, `/rename`, `/expire` handlers.
const ActiveProject = struct { client: *server.Client, id: []const u8 };
fn requireActiveProject(session: *Session) ?ActiveProject {
    if (!session.hasRemote()) {
        logo.print("error: no active server project — '/fetch <name>' first\n", .{});
        return null;
    }
    const client = session.findServer(session.remote_url.?) orelse {
        logo.print("error: the active project's server is not connected — '/reconnect' first\n", .{});
        return null;
    };
    return .{ .client = client, .id = session.remote_id.? };
}

pub fn doProjectColor(session: *Session, arg: []const u8) !void {
    const active = requireActiveProject(session) orelse return;
    const client = active.client;
    const id = active.id;
    const trimmed = std.mem.trim(u8, arg, " \t");

    // No argument: read and show the project's current colour, rendered in it.
    if (trimmed.len == 0) {
        const color = client.getProjectColor(id) catch |e| {
            logo.print("error: could not read the project colour ({s})\n", .{@errorName(e)});
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
            logo.print("error: invalid colour '{s}' — give a '#rrggbb' / name, or 'clear'\n", .{trimmed});
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
        logo.print("error: could not read the blank colour ({s})\n", .{@errorName(e)});
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
        logo.print("error: this project is not a blank image — nothing to recolour\n", .{});
        return;
    }
    var hexbuf: [8]u8 = undefined;
    const col = core.parseColor(session.gpa, trimmed) orelse {
        logo.print("error: invalid colour '{s}' — give a '#rrggbb' / name\n", .{trimmed});
        return;
    };
    const color = std.fmt.bufPrint(&hexbuf, "#{x:0>2}{x:0>2}{x:0>2}", .{ col.r, col.g, col.b }) catch "#000000";
    if (!putProjectField(session, client, id, color, .blank_color)) return; // message already printed
    printProjectColor("blank colour set to", color);
}

/// `/project-description [<text...>]` — set (or, with no text, clear) the active server project's
/// free-text description. The current value is shown in the `/projects` listing (a trailing note),
/// so this command is set/clear only; the whole argument is the description verbatim. A ~2000-char
/// soft cap guards against pathological input (the core imposes no length limit).
pub fn doProjectDescription(session: *Session, arg: []const u8) !void {
    const active = requireActiveProject(session) orelse return;
    const client = active.client;
    const id = active.id;
    const trimmed = std.mem.trim(u8, arg, " \t");
    if (trimmed.len > 2000) {
        logo.print("error: description too long ({d} bytes) — keep it under 2000\n", .{trimmed.len});
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

/// Version-guarded PUT of one project metadata field (colour or name) with a 409 retry (a peer
/// saved first → re-read the version and retry), mirroring pushLayout. Advances the LWW guard so
/// the server's echo of our own change isn't mistaken for a peer edit. Returns success; prints on
/// a hard failure.
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
                .color => logo.print("error: could not set the project colour ({s})\n", .{@errorName(e)}),
                .name => logo.print("error: could not rename the project ({s})\n", .{@errorName(e)}),
                .blank_color => logo.print("error: could not set the blank colour ({s})\n", .{@errorName(e)}),
                .description => logo.print("error: could not set the description ({s})\n", .{@errorName(e)}),
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
            logo.print("error: could not update keywords ({s})\n", .{@errorName(e)});
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
            logo.print("error: could not query {s} ({s})\n", .{ c.base, @errorName(e) });
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
        logo.print("error: could not read keywords for \"{s}\" ({s})\n", .{ name, @errorName(e) });
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
                    logo.print("error: could not read keywords for \"{s}\" ({s})\n", .{ name, @errorName(e) });
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
            logo.print("error: could not list projects on {s} ({s})\n", .{ c.base, @errorName(e) });
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
        logo.print("no server connections — use '/connect <url>'\n", .{});
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
        logo.print("error: give a new name — e.g. '/rename MyProject'\n", .{});
        return;
    }
    if (!putProjectField(session, active.client, active.id, name, .name)) return;
    session.setLabel(name) catch {};
    logo.print("renamed to \"{s}\"\n", .{name});
    ui.status(session); // reprint "image: <name> …" with the new name
}

/// `/expire [<duration>]` — set when the active server project expires, from a free-form
/// duration parsed by the shared core ("days 23", "months 3", "fortnight", "month", "off").
/// With no argument it prints the accepted formats. A valid spec is resolved to an absolute
/// time (now + duration) and PUT to the server version-guarded; "off"/"never" clears it (0 =
/// keep forever). Server projects have no expiry until one is set this way.
pub fn doExpire(session: *Session, io: std.Io, arg: []const u8) !void {
    const spec = std.mem.trim(u8, arg, " \t");
    if (spec.len == 0) {
        printExpireFormats();
        return;
    }
    const active = requireActiveProject(session) orelse return;
    const client = active.client;
    const ms = core.parseDuration(session.gpa, spec) orelse {
        logo.print("error: invalid duration '{s}'\n", .{spec});
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
            logo.print("error: could not set the project expiry ({s})\n", .{@errorName(e)});
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
            logo.print("error: no connections — '/connect <url>' first\n", .{});
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
            logo.print("error: not connected to {s} — '/connect' first\n", .{base});
            return;
        };
    } else if (session.servers.items.len == 1) {
        client = &session.servers.items[0];
    } else if (session.servers.items.len == 0) {
        logo.print("error: no connections — '/connect <url>' first\n", .{});
        return;
    } else {
        logo.print("error: multiple servers connected — give a URL: '/fetch {s} <url>'\n", .{name});
        return;
    }

    const ref = (client.findProjectRef(name) catch |e| {
        logo.print("error: server lookup failed ({s})\n", .{@errorName(e)});
        return;
    }) orelse {
        logo.print("error: no project named \"{s}\" on {s}\n", .{ name, client.base });
        return;
    };
    defer session.gpa.free(ref.id);

    const bytes = client.downloadFile(ref.id, "original") catch |e| {
        logo.print("error: could not download image ({s})\n", .{@errorName(e)});
        return;
    };
    defer session.gpa.free(bytes);
    const img = image.decode(session.gpa, bytes) catch |e| {
        logo.print("error: could not decode server image ({s})\n", .{@errorName(e)});
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
    adoptServerLayout(session, client, ref.id); // show the project's stored crop/rotation/filter/lines
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
        logo.print("error: sync takes 'on', 'off', or nothing (to toggle)\n", .{});
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

/// What to do with one incoming project event for the active project. Kept pure (no I/O,
/// no session) so the live-edit decision is unit-tested without a socket or a server.
pub const PullAction = enum {
    ignore, // not our project, or an edit we already hold (incl. our own echoed push)
    pull, // a newer peer edit and no local edits pending — take it
    warn_dirty, // a newer peer edit but we have unsynced local edits — don't clobber, warn
    deleted, // the active project was deleted on the server
};

pub fn pullAction(remote_active: bool, ids_match: bool, deleted: bool, ev_version: i64, remote_version: i64, dirty: bool) PullAction {
    if (!remote_active or !ids_match) return .ignore;
    if (deleted) return .deleted;
    if (ev_version <= remote_version) return .ignore; // older, or our own push echoed back
    if (dirty) return .warn_dirty;
    return .pull;
}

/// Clear the current terminal line ONCE before emitting async output at the prompt (tracked via
/// `done`), so the caller only repaints — and a no-op poll prints nothing → no idle flicker.
fn clearPromptLine(done: *bool) void {
    if (done.*) return;
    logo.print("\r\x1b[K", .{});
    done.* = true;
}

/// Drain pending project-update events from the live feed and act on ones touching the active
/// project: auto-pull a peer's newer image into the working session (live editing), reflect a
/// peer's name/colour change in the header, or — when we have unsynced local edits — warn instead
/// of clobbering them. Each message shows when the change happened, never an internal version
/// number. Called at the REPL prompt boundary; best-effort and never blocks. Returns true if it
/// printed anything (so the line editor repaints the prompt only then — no idle flicker otherwise).
pub fn pollEvents(session: *Session, io: std.Io) bool {
    if (session.events == null) return false;
    const now = std.Io.Clock.real.now(io).toMilliseconds();
    var printed = false;
    while (session.events.?.poll() catch null) |ev| {
        var e = ev;
        defer e.deinit(session.gpa);
        const ids_match = session.remote_id != null and std.mem.eql(u8, e.id, session.remote_id.?);
        switch (pullAction(session.hasRemote(), ids_match, e.deleted, e.version, session.remote_version, session.dirty)) {
            .ignore => {},
            .deleted => {
                clearPromptLine(&printed);
                logo.print("✗ \"{s}\" was deleted on the server\n", .{e.name});
            },
            .pull => {
                session.remote_version = e.version;
                if (session.sync) {
                    // Live editing: pull the peer's image + layout (also refreshes name + colour, reprints).
                    clearPromptLine(&printed);
                    pullActive(session, &e, now);
                } else if (applyMetaUpdate(session, e.name)) {
                    // Sync off: never pull image/layout edits, but always reflect a peer's NAME/COLOUR.
                    // Refresh the WHOLE view rather than stacking a new "image: …" line under the old one.
                    printed = true;
                    ui.redraw(session);
                }
            },
            .warn_dirty => {
                session.remote_version = e.version;
                _ = applyMetaUpdate(session, e.name); // metadata is cheap and clobbers nothing
                clearPromptLine(&printed);
                var tb: [32]u8 = undefined;
                logo.print(
                    "↺ \"{s}\" changed on the server ({s}) — you have local edits; '/save' to push yours or '/fetch' to take theirs\n",
                    .{ e.name, server.formatAgo(&tb, now, e.updated_at) },
                );
            },
        }
    }
    return printed;
}

/// Refresh the active project's displayed name + colour from a peer's metadata change. `name` is
/// the event's (canonical) name; the colour is re-read from the server. Returns true when either
/// actually changed (so the caller only reprints on a real change). Cheap — no image download.
fn applyMetaUpdate(session: *Session, name: []const u8) bool {
    var changed = false;
    if (name.len != 0 and (session.label == null or !std.mem.eql(u8, session.label.?, name))) {
        session.setLabel(name) catch {};
        changed = true;
    }
    const client = session.findServer(session.remote_url.?) orelse return changed;
    if (client.getProjectColor(session.remote_id.?)) |c| {
        defer session.gpa.free(c);
        const old = session.remote_color orelse "";
        if (!std.mem.eql(u8, old, c)) {
            session.setRemoteColor(c) catch {};
            changed = true;
        }
    } else |_| {}
    return changed;
}

/// Replace the working image with the active project's latest server image (a peer's edit).
/// Resets the undo history to the pulled image and clears the dirty flag — the session now
/// matches the server. Keeps the active-remote/events binding intact.
fn pullActive(session: *Session, e: *const server.Event, now: i64) void {
    const client = session.findServer(session.remote_url.?) orelse return;
    // Pull the ORIGINAL + the layout and rebuild the view from them (rotate/crop/filter/lines),
    // the same way the GUIs reconstruct a peer's change — never the baked result.
    const bytes = client.downloadFile(session.remote_id.?, "original") catch |err| {
        logo.print("↺ \"{s}\" changed but the image could not be pulled ({s})\n", .{ e.name, @errorName(err) });
        return;
    };
    defer session.gpa.free(bytes);
    const img = image.decode(session.gpa, bytes) catch |err| {
        logo.print("↺ pull failed: could not decode the server image ({s})\n", .{@errorName(err)});
        return;
    };
    session.loadImage(img, e.name, true, session.default_fmt, null) catch |err| {
        logo.print("↺ pull failed ({s})\n", .{@errorName(err)});
        return;
    };
    adoptServerLayout(session, client, session.remote_id.?); // apply the peer's crop/rotation/filter/lines
    // A peer may also have recoloured the project — refresh so the header repaints in it.
    if (client.getProjectColor(session.remote_id.?)) |c| {
        defer session.gpa.free(c);
        session.setRemoteColor(c) catch {};
    } else |_| {}
    session.dirty = false; // the working image now matches the server
    var tb: [32]u8 = undefined;
    ui.redraw(session);
    logo.print("↺ pulled \"{s}\" from the server (changed {s})\n", .{ e.name, server.formatAgo(&tb, now, e.updated_at) });
}

// ── /sync debounce ─────────────────────────────────────────────────────────────
//
// Uploading after every edit would re-encode + re-upload the whole image once per action.
// Instead each edit sets a cheap `dirty` flag (markDirty) and the REPL flushes it with one
// upload once the input burst settles (flushSync, at the prompt boundary). `input_pending`
// keeps the upload deferred while more commands remain buffered, so a run of edits coalesces.

/// Queue a sync upload for the current result. Cheap and synchronous; the actual upload is
/// deferred to `flushSync`. No-op unless sync is on and a server project is active.
pub fn markDirty(session: *Session) void {
    if (session.sync and session.hasRemote()) session.dirty = true;
}

/// Pure debounce decision: upload only when sync is on, a project is active, an edit is
/// pending, and the input burst has settled (no more buffered commands). Unit-tested.
pub fn shouldFlush(sync: bool, has_remote: bool, dirty: bool, input_pending: bool) bool {
    return sync and has_remote and dirty and !input_pending;
}

/// Flush a pending sync upload when the burst has settled. `input_pending` is true when the
/// REPL still has buffered input to process, coalescing a run of edits into one upload.
pub fn flushSync(session: *Session, input_pending: bool) void {
    if (!shouldFlush(session.sync, session.hasRemote(), session.dirty, input_pending)) return;
    session.dirty = false;
    pushResult(session);
}

/// Push the current edit state to the active project: the full structured LAYOUT (lines +
/// filter + crop + rotation) so every CLI edit shows live in open browser/desktop editors (they
/// render original + layout, not the baked result), then the rendered `result` raster (used by
/// the GUIs only for the projects-list thumbnail). Shared by the `/sync` flush and `/save`.
fn pushResult(session: *Session) void {
    if (!session.hasImage() or !session.hasRemote()) return;
    const client = session.findServer(session.remote_url.?) orelse return;
    const id = session.remote_id.?;
    pushLayout(session, client, id);
    const img = session.current();
    const result = image.encode(session.gpa, img.*, session.default_fmt) catch return;
    defer session.gpa.free(result);
    client.uploadFile(id, "result", result, session.default_fmt.ext(), img.width, img.height) catch |e| {
        logo.print("sync: upload failed ({s})\n", .{@errorName(e)});
        return;
    };
    // Advance the LWW guard to the version our push produced, so the server's echo of our own
    // change (which arrives on the events feed) isn't mistaken for a peer edit to pull.
    if (client.getProjectVersion(id)) |v| {
        session.remote_version = v;
    } else |_| {}
    logo.print("synced to {s}\n", .{client.base});
}

/// PUT the current structured layout (version-guarded). On a 409 (a peer saved first) re-read
/// the version and retry — last-writer-wins for the CLI's edits (a fetched project already
/// carries the server's lines/geometry, so a normal push preserves them).
fn pushLayout(session: *Session, client: *server.Client, id: []const u8) void {
    var tries: u8 = 0;
    while (tries < 4) : (tries += 1) {
        const layout = session.currentLayoutJson() catch return;
        defer session.gpa.free(layout);
        client.updateProject(id, layout, session.remote_version) catch |e| {
            if (e == server.Error.Conflict) {
                if (client.getProjectVersion(id)) |v| {
                    session.remote_version = v;
                    continue; // re-read won the race; retry the PUT
                } else |_| return;
            }
            logo.print("sync: layout update failed ({s})\n", .{@errorName(e)});
            return;
        };
        if (client.getProjectVersion(id)) |v| {
            session.remote_version = v;
        } else |_| {}
        return;
    }
}

/// Fetch the active project's stored layout and adopt it into the session (crop/rotation/filter/
/// lines), so a fetched/pulled project's full state shows — not just the bare original.
fn adoptServerLayout(session: *Session, client: *server.Client, id: []const u8) void {
    const body = client.getProject(id) catch return;
    defer session.gpa.free(body);
    const layout_json = extractLayoutObject(session.gpa, body) catch return;
    defer session.gpa.free(layout_json);
    session.adoptServerLayout(layout_json) catch {};
}

/// Pull the `layout` object out of a GET /projects/{id} response as its own JSON string ("{}"
/// when absent). Caller owns the result.
fn extractLayoutObject(gpa: std.mem.Allocator, body: []const u8) ![]u8 {
    var parsed = std.json.parseFromSlice(std.json.Value, gpa, body, .{}) catch return server.Error.BadResponse;
    defer parsed.deinit();
    if (parsed.value == .object) {
        if (parsed.value.object.get("layout")) |lv| {
            if (lv == .object) return std.json.Stringify.valueAlloc(gpa, lv, .{});
        }
    }
    return gpa.dupe(u8, "{}");
}

// ── transforms (crop / rotate / filter / layout, all undoable) ─────────────────
//
// Each transform updates the session's STRUCTURED edit state (rotation/crop/filter/lines) and
// rebuilds the derived view — so the exact edit serializes to a browser-compatible layout and
// shows live in open GUI editors, not just baked into the result raster.

/// Map a /filter argument ("bw"|"sepia"|"invert"|"contour"|"none"|<colour>) onto the layout
/// filter and apply it. The named modes are checked before the colour fallback. Returns
/// false for an unrecognized argument (the caller reports the error).
fn applyFilterArg(session: *Session, arg: []const u8) bool {
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
            logo.print("error: apply needs a path or URL to a layout JSON — e.g. '/apply notes.json'\n", .{});
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
                logo.print("error: rotate needs an integer (quarter-turns), e.g. '/rotate -1'\n", .{});
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
                logo.print("error: unknown filter \"{s}\" — 'bw', 'sepia', 'invert', 'contour', 'none', or a colour\n", .{action.arg});
                return false;
            }
            ui.ack(session, action.arg);
        },
        .layout => {
            const bytes = pipeline.loadLayoutBytes(session.gpa, io, action.arg) catch return false; // msg printed
            defer session.gpa.free(bytes);
            session.addLines(bytes) catch return false;
            // Adopt the layout file's embedded filter, if any (layout.zig top-level "filter").
            var L = layout_mod.parse(session.gpa, bytes) catch {
                ui.ack(session, "drawn");
                return true; // the lines were added even if the filter parse failed
            };
            defer L.deinit();
            if (L.filter) |f| _ = applyFilterArg(session, f);
            ui.ack(session, "drawn");
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
    if (!session.hasImage()) {
        logo.print("no image loaded\n", .{});
        return;
    }
    session.revert();
    ui.redraw(session);
    logo.print("reset to original\n", .{});
}

pub fn doDrop(session: *Session) void {
    if (!session.hasImage()) {
        logo.print("no image loaded\n", .{});
        return;
    }
    session.clearAll();
    ui.redraw(session); // header now reads "(none)"
}

pub fn doCopy(session: *Session, io: std.Io) !void {
    if (!session.hasImage()) return ui.noImage();
    const img = session.current().*;
    const png = image.encode(session.gpa, img, .png) catch |e| {
        logo.print("error: could not encode the image for the clipboard ({s})\n", .{@errorName(e)});
        return;
    };
    defer session.gpa.free(png);
    clipboard.writeImage(session.gpa, io, png) catch |e| return clipError("copy", e);
    logo.print("copied to clipboard ({d}x{d})\n", .{ img.width, img.height });
}

pub fn doPaste(session: *Session, io: std.Io) !void {
    const bytes = clipboard.readImage(session.gpa, io) catch |e| return clipError("paste", e);
    defer session.gpa.free(bytes);
    const img = image.decode(session.gpa, bytes) catch |e| {
        logo.print("error: the clipboard image could not be decoded ({s})\n", .{@errorName(e)});
        return;
    };
    try session.loadImage(img, "clipboard", true, .png, null);
    ui.redraw(session);
}

fn clipError(verb: []const u8, e: anyerror) void {
    switch (e) {
        clipboard.Error.Unsupported => logo.print("error: clipboard {s} is only supported on macOS\n", .{verb}),
        clipboard.Error.ToolMissing => logo.print("error: 'osascript' not found — clipboard {s} needs macOS\n", .{verb}),
        clipboard.Error.NoImage => logo.print("error: no image on the clipboard to paste\n", .{}),
        else => logo.print("error: clipboard {s} failed ({s})\n", .{ verb, @errorName(e) }),
    }
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

    logo.print("error: unknown theme '{s}' — type '/theme' to list them, or give a colour like #ff5623\n", .{arg});
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
    const a = theme.find(key) orelse theme.accents[0];
    applyAccent(session, a.rgb, a.key, a.hex, false);
}

/// Set a random vivid custom colour (outside the preset list) — the double-click-on-logo
/// behaviour, the terminal stand-in for the browser logo's colour picker. Silent. `seed` varies
/// per click (the caller passes the click time) so each double-click yields a different hue.
pub fn randomCustomTheme(session: *Session, seed: u64) void {
    var prng = std.Random.DefaultPrng.init(seed);
    const h = @as(f64, @floatFromInt(prng.random().intRangeLessThan(u16, 0, 360)));
    const rgb = hsvToRgb(h, 0.7, 0.95); // always vivid + readable, essentially never a preset
    var hexbuf: [8]u8 = undefined;
    const hex = std.fmt.bufPrint(&hexbuf, "#{x:0>2}{x:0>2}{x:0>2}", .{ rgb[0], rgb[1], rgb[2] }) catch "#??????";
    applyAccent(session, rgb, hex, hex, false);
}

fn hsvToRgb(h: f64, s: f64, v: f64) [3]u8 {
    const c = v * s;
    const hp = h / 60.0;
    const x = c * (1.0 - @abs(@mod(hp, 2.0) - 1.0));
    var r: f64 = 0;
    var g: f64 = 0;
    var b: f64 = 0;
    if (hp < 1) {
        r = c;
        g = x;
    } else if (hp < 2) {
        r = x;
        g = c;
    } else if (hp < 3) {
        g = c;
        b = x;
    } else if (hp < 4) {
        g = x;
        b = c;
    } else if (hp < 5) {
        r = x;
        b = c;
    } else {
        r = c;
        b = x;
    }
    const m = v - c;
    return .{
        @intFromFloat(@round((r + m) * 255.0)),
        @intFromFloat(@round((g + m) * 255.0)),
        @intFromFloat(@round((b + m) * 255.0)),
    };
}

/// `/mouse [on|off]` (bare toggles): enable or disable mouse reporting in full-screen mode.
/// Turning it OFF hands the mouse back to the terminal for native select/copy; turning it ON
/// re-enables logo clicks + wheel scrolling + the visual drag-selection. No-op (with a note)
/// outside full-screen.
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

test "pullAction: live-pull a newer peer edit, warn on local edits, ignore self/old" {
    // No active project, or an event for a different project → ignore.
    try testing.expectEqual(PullAction.ignore, pullAction(false, false, false, 5, 0, false));
    try testing.expectEqual(PullAction.ignore, pullAction(true, false, false, 5, 0, false));

    // A newer peer edit with no pending local edits → pull it.
    try testing.expectEqual(PullAction.pull, pullAction(true, true, false, 5, 4, false));

    // A newer peer edit but we have unsynced local edits → warn, don't clobber.
    try testing.expectEqual(PullAction.warn_dirty, pullAction(true, true, false, 5, 4, true));

    // Our own push echoed back (version not newer than what we hold) → ignore, even dirty.
    try testing.expectEqual(PullAction.ignore, pullAction(true, true, false, 4, 4, false));
    try testing.expectEqual(PullAction.ignore, pullAction(true, true, false, 3, 4, true));

    // A delete of the active project is surfaced regardless of version/dirty.
    try testing.expectEqual(PullAction.deleted, pullAction(true, true, true, 9, 4, true));
}

test "shouldFlush only uploads when on, active, dirty, and the burst has settled" {
    // The happy path: all preconditions met and no more buffered input.
    try testing.expect(shouldFlush(true, true, true, false));
    // Deferred while more commands are still queued (coalesce the burst into one upload).
    try testing.expect(!shouldFlush(true, true, true, true));
    // Each precondition is necessary.
    try testing.expect(!shouldFlush(false, true, true, false)); // sync off
    try testing.expect(!shouldFlush(true, false, true, false)); // no active project
    try testing.expect(!shouldFlush(true, true, false, false)); // nothing pending
}

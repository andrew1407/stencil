//! Console command implementations: one handler per verb/transform. Each drives the same
//! pipeline.zig building blocks the flag mode uses, snapshots the result into the session's
//! undo history, and reports via ui.zig. Pure parsing lives in commands.zig; this file is
//! the I/O-bearing half (load/save/clipboard/theme + the undoable transforms).
const std = @import("std");
const image = @import("../image.zig");
const pipeline = @import("../pipeline.zig");
const net = @import("../net.zig");
const server = @import("../server.zig");
const logo = @import("../logo.zig");
const core = @import("../core.zig");
const theme = @import("../theme.zig");
const clipboard = @import("../clipboard.zig");
const commands = @import("commands.zig");
const layout_mod = @import("../layout.zig");
const ui = @import("ui.zig");
const Session = @import("session.zig").Session;
const Action = commands.Action;

// ── source / save ─────────────────────────────────────────────────────────────

pub fn doUpload(session: *Session, io: std.Io, arg: []const u8) !void {
    if (arg.len == 0) {
        logo.print("error: upload needs a path or URL — e.g. '/upload photo.png'\n", .{});
        return;
    }
    const src = pipeline.acquireInput(session.gpa, io, arg, 0) catch return; // message already printed
    try session.loadImage(src.img, arg, net.isUrl(arg), src.default_fmt);
    ui.redraw(session); // show the freshly loaded image on top
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
    try session.loadImage(img, "blank", true, .png);
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
/// from. `color` is the project's custom name colour ("" = none → paint in the theme accent).
const ProjectRow = struct { name: []u8, size: []u8, created: []u8, expires: []u8, changed: []u8, color: []u8, server: []const u8 };

fn freeRows(gpa: std.mem.Allocator, rows: *std.ArrayList(ProjectRow)) void {
    for (rows.items) |r| {
        gpa.free(r.name);
        gpa.free(r.size);
        gpa.free(r.created);
        gpa.free(r.expires);
        gpa.free(r.changed);
        gpa.free(r.color);
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
        try rows.append(gpa, .{ .name = name, .size = size, .created = created, .expires = expires, .changed = changed, .color = color, .server = if (multi) client.base else "" });
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
    printRow(gpa, "NAME", "", nw, "SIZE", sw, "CREATED", crw, "EXPIRES", erw, "CHANGED", cw, if (multi) "SERVER" else null); // header: no colour
    for (rows) |r| {
        var buf: [20]u8 = undefined;
        printRow(gpa, r.name, theme.nameSeq(r.color, &buf), nw, r.size, sw, r.created, crw, r.expires, erw, r.changed, cw, if (multi) r.server else null);
    }
}

/// Print one table row, padding each non-final column to its width. `name_seq` colours the NAME
/// column ("" = plain). Best-effort.
fn printRow(gpa: std.mem.Allocator, name: []const u8, name_seq: []const u8, nw: usize, size: []const u8, sw: usize, created: []const u8, crw: usize, expires: []const u8, erw: usize, changed: []const u8, cw: usize, srv: ?[]const u8) void {
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
    logo.print("{s}\n", .{line.items});
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
pub fn doProjectColor(session: *Session, arg: []const u8) !void {
    if (!session.hasRemote()) {
        logo.print("error: no active server project — '/fetch <name>' first\n", .{});
        return;
    }
    const client = session.findServer(session.remote_url.?) orelse {
        logo.print("error: the active project's server is not connected — '/reconnect' first\n", .{});
        return;
    };
    const id = session.remote_id.?;
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

/// A reset keyword for `/project-color`: clears the custom colour back to the theme accent.
fn isClearWord(s: []const u8) bool {
    const eq = std.ascii.eqlIgnoreCase;
    return eq(s, "clear") or eq(s, "none") or eq(s, "default");
}

const ProjectField = enum { color, name };

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

/// `/rename <new name>` — rename the active fetched project, pushed live to the server
/// (version-guarded). Updates the displayed label + reprints the header.
pub fn doRename(session: *Session, arg: []const u8) !void {
    if (!session.hasRemote()) {
        logo.print("error: no active server project — '/fetch <name>' first\n", .{});
        return;
    }
    const client = session.findServer(session.remote_url.?) orelse {
        logo.print("error: the active project's server is not connected — '/reconnect' first\n", .{});
        return;
    };
    const name = std.mem.trim(u8, arg, " \t");
    if (name.len == 0) {
        logo.print("error: give a new name — e.g. '/rename MyProject'\n", .{});
        return;
    }
    if (!putProjectField(session, client, session.remote_id.?, name, .name)) return;
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
    if (!session.hasRemote()) {
        logo.print("error: no active server project — '/fetch <name>' first\n", .{});
        return;
    }
    const client = session.findServer(session.remote_url.?) orelse {
        logo.print("error: the active project's server is not connected — '/reconnect' first\n", .{});
        return;
    };
    const ms = core.parseDuration(session.gpa, spec) orelse {
        logo.print("error: invalid duration '{s}'\n", .{spec});
        printExpireFormats();
        return;
    };
    const now = std.Io.Clock.real.now(io).toMilliseconds();
    const expires_at: i64 = if (ms == 0) 0 else now + ms;
    if (!putProjectExpiry(session, client, session.remote_id.?, expires_at)) return; // message printed
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
    try session.loadImage(img, name, true, .png);
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
    session.loadImage(img, e.name, true, session.default_fmt) catch |err| {
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
    try session.loadImage(img, "clipboard", true, .png);
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
        applyAccent(session, a.rgb, a.key, a.hex);
        return;
    }

    // Otherwise accept any colour the core understands — a '#rrggbb' hex or a CSS name.
    if (core.parseColor(session.gpa, arg)) |c| {
        var hexbuf: [8]u8 = undefined;
        const hex = std.fmt.bufPrint(&hexbuf, "#{x:0>2}{x:0>2}{x:0>2}", .{ c.r, c.g, c.b }) catch "#??????";
        applyAccent(session, .{ c.r, c.g, c.b }, hex, hex);
        return;
    }

    logo.print("error: unknown theme '{s}' — type '/theme' to list them, or give a colour like #ff5623\n", .{arg});
}

// Repaint everything in a new accent: the logo's RGB, the stored label, the screen, and a
// confirmation line. `label` is the preset key or a custom '#hex'; `hex` is shown in the message.
fn applyAccent(session: *Session, rgb: [3]u8, label: []const u8, hex: []const u8) void {
    logo.setAccent(rgb);
    ui.setAccent(label);
    ui.redraw(session); // repaint the logo outline in the new accent
    logo.print("theme set to {s} ({s})\n", .{ label, hex });
}

// ── tests (pure routing / debounce logic) ──────────────────────────────────────

const testing = std.testing;

test "saveTarget routes a path to local, a bare save to the active project, else none" {
    // An explicit path always saves locally, regardless of an active remote.
    try testing.expectEqual(SaveTarget.local, saveTarget("out.png".len, false));
    try testing.expectEqual(SaveTarget.local, saveTarget("out.png".len, true));
    // A bare /save pushes to the active server project when one is set.
    try testing.expectEqual(SaveTarget.server, saveTarget(0, true));
    // A bare /save with nothing to write to is an error.
    try testing.expectEqual(SaveTarget.none, saveTarget(0, false));
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

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
    const blank = commands.parseBlank(session.gpa, arg) orelse {
        logo.print("error: blank takes '[w h] [color]' — e.g. '/blank 800 600 white'\n", .{});
        return;
    };
    const img = try pipeline.acquireBlank(session.gpa, blank);
    try session.loadImage(img, "blank", true, .png);
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
            pipeline.writeOutput(session.gpa, io, session.current().*, arg, session.default_fmt) catch return;
            // When syncing, a local save also queues a push of the result to the active project.
            markDirty(session);
        },
    }
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

/// `/connections` — list the connected servers.
pub fn doConnections(session: *Session) void {
    if (session.servers.items.len == 0) {
        logo.print("no server connections — use '/connect <url>'\n", .{});
        return;
    }
    logo.print("connections ({d}):\n", .{session.servers.items.len});
    for (session.servers.items) |*c| {
        const active = session.remote_url != null and std.mem.eql(u8, session.remote_url.?, c.base);
        logo.print("  {s}{s}\n", .{ c.base, if (active) "  (active project)" else "" });
    }
}

/// `/fetch <project name> [url]` — load a server project's image to continue editing.
pub fn doFetch(session: *Session, arg: []const u8) !void {
    if (arg.len == 0) {
        logo.print("error: fetch needs a project name — e.g. '/fetch MyProject'\n", .{});
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

    const id = (client.findProjectIdByName(name) catch |e| {
        logo.print("error: server lookup failed ({s})\n", .{@errorName(e)});
        return;
    }) orelse {
        logo.print("error: no project named \"{s}\" on {s}\n", .{ name, client.base });
        return;
    };
    defer session.gpa.free(id);

    const bytes = client.downloadFile(id, "original") catch |e| {
        logo.print("error: could not download image ({s})\n", .{@errorName(e)});
        return;
    };
    defer session.gpa.free(bytes);
    const img = image.decode(session.gpa, bytes) catch |e| {
        logo.print("error: could not decode server image ({s})\n", .{@errorName(e)});
        return;
    };
    try session.loadImage(img, name, true, .png);
    try session.setRemote(client.base, id);
    // While syncing, open a live read-only events feed so concurrent saves by other
    // clients to this project are surfaced (see pollEvents). Best-effort.
    if (session.sync) session.openEvents(client);
    ui.redraw(session);
    logo.print("fetched \"{s}\" from {s} (sync {s})\n", .{ name, client.base, if (session.sync) "on" else "off" });
}

/// `/sync on|off` — when on, every edit (and save) uploads the result to the active project.
pub fn doSync(session: *Session, arg: []const u8) void {
    const a = std.mem.trim(u8, arg, " \t");
    if (std.ascii.eqlIgnoreCase(a, "on") or std.ascii.eqlIgnoreCase(a, "true")) {
        session.sync = true;
    } else if (std.ascii.eqlIgnoreCase(a, "off") or std.ascii.eqlIgnoreCase(a, "false")) {
        session.sync = false;
    } else if (a.len == 0) {
        logo.print("sync is {s}\n", .{if (session.sync) "on" else "off"});
        return;
    } else {
        logo.print("error: sync takes 'on' or 'off'\n", .{});
        return;
    }
    logo.print("sync {s}\n", .{if (session.sync) "on" else "off"});
    if (session.sync and session.remote_id == null)
        logo.print("  (no active server project yet — use '/fetch <name>')\n", .{});
    // Open/close the live events feed to match the sync state for the active project.
    if (session.sync) {
        if (session.remote_url) |u| {
            if (session.findServer(u)) |client| session.openEvents(client);
        }
    } else {
        session.closeEvents();
    }
}

/// Drain any pending project-update events from the live feed and surface ones that
/// touch the active project — telling the user the server image changed under them.
/// Called at the REPL prompt boundary; best-effort and never blocks.
pub fn pollEvents(session: *Session) void {
    if (session.events == null) return;
    while (session.events.?.poll() catch null) |ev| {
        var e = ev;
        defer e.deinit(session.gpa);
        if (session.remote_id != null and std.mem.eql(u8, e.id, session.remote_id.?)) {
            logo.print("↺ \"{s}\" was updated on the server (v{d}) — '/fetch' to refresh\n", .{ e.name, e.version });
        }
    }
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

/// Encode the current image and upload it as the active project's `result`. Assumes a remote
/// is active; prints on failure. Shared by the `/sync` flush and the manual `/save` push.
fn pushResult(session: *Session) void {
    if (!session.hasImage() or !session.hasRemote()) return;
    const client = session.findServer(session.remote_url.?) orelse return;
    const img = session.current();
    const result = image.encode(session.gpa, img.*, session.default_fmt) catch return;
    defer session.gpa.free(result);
    client.uploadFile(session.remote_id.?, "result", result, session.default_fmt.ext(), img.width, img.height) catch |e| {
        logo.print("sync: upload failed ({s})\n", .{@errorName(e)});
        return;
    };
    logo.print("synced result to {s}\n", .{client.base});
}

// ── transforms (crop / rotate / filter / layout, all undoable) ─────────────────

pub fn runAction(session: *Session, io: std.Io, action: Action) !void {
    if (!session.hasImage()) return ui.noImage();
    switch (action.kind) {
        .crop => {
            var album = false;
            const spec = commands.stripAlbum(session.gpa, action.arg, &album) catch return;
            defer session.gpa.free(spec);
            var work = try session.workCopy();
            pipeline.applyCropSpec(session.gpa, &work, spec, album) catch {
                work.deinit(session.gpa);
                return;
            };
            try session.commit(work);
            ui.ack(session, "cropped");
        },
        .rotate => {
            const n = std.fmt.parseInt(i32, action.arg, 10) catch {
                logo.print("error: rotate needs an integer (quarter-turns), e.g. '/rotate -1'\n", .{});
                return;
            };
            if (@mod(n, 4) == 0) {
                logo.print("rotate {d} is a full turn — no change\n", .{n});
                return;
            }
            var work = try session.workCopy();
            pipeline.applyRotateBy(session.gpa, &work, n) catch {
                work.deinit(session.gpa);
                return;
            };
            try session.commit(work);
            ui.ack(session, "rotated");
        },
        .filter => {
            if (action.arg.len == 0) {
                logo.print("error: filter needs a mode — 'bw', 'sepia', 'none', or a colour\n", .{});
                return;
            }
            var work = try session.workCopy();
            pipeline.applyFilterMode(session.gpa, &work, action.arg);
            try session.commit(work);
            ui.ack(session, action.arg);
        },
        .layout => {
            if (action.arg.len == 0) {
                logo.print("error: apply needs a path or URL to a layout JSON\n", .{});
                return;
            }
            var work = try session.workCopy();
            const filter = pipeline.applyLayoutSrc(session.gpa, io, &work, action.arg) catch {
                work.deinit(session.gpa);
                return;
            };
            defer if (filter) |f| session.gpa.free(f);
            if (filter) |f| pipeline.applyFilterMode(session.gpa, &work, f);
            try session.commit(work);
            ui.ack(session, "drawn");
        },
    }
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

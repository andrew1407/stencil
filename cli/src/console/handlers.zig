//! Console command implementations: one handler per verb/transform. Each drives the same
//! pipeline.zig building blocks the flag mode uses, snapshots the result into the session's
//! undo history, and reports via ui.zig. Pure parsing lives in commands.zig; this file is
//! the I/O-bearing half (load/save/clipboard/theme + the undoable transforms).
const std = @import("std");
const image = @import("../image.zig");
const pipeline = @import("../pipeline.zig");
const net = @import("../net.zig");
const logo = @import("../logo.zig");
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

pub fn doSave(session: *Session, io: std.Io, arg: []const u8) !void {
    if (!session.hasImage()) return ui.noImage();
    if (arg.len == 0) {
        logo.print("error: save needs an output path — e.g. '/save out.png'\n", .{});
        return;
    }
    pipeline.writeOutput(session.gpa, io, session.current().*, arg, session.default_fmt) catch return;
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
    const a = theme.find(arg) orelse {
        logo.print("error: unknown theme '{s}' — type '/theme' to list them\n", .{arg});
        return;
    };
    logo.setAccent(a.rgb);
    ui.setAccent(a.key);
    ui.redraw(session); // repaint the logo outline in the new accent
    logo.print("theme set to {s} ({s})\n", .{ a.label, a.hex });
}

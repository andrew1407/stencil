//! The undoable transforms — crop, rotate, filter, apply-layout — behind `/exec` and the
//! bare verbs. Each updates the session's STRUCTURED edit state and rebuilds the derived
//! view, so the exact edit serializes to a browser-compatible layout.
const std = @import("std");
const pipeline = @import("../../pipeline.zig");
const logo = @import("../../app/logo.zig");
const core = @import("../../core.zig");
const commands = @import("../commands.zig");
const layout_mod = @import("../../media/layout.zig");
const msg = @import("../../app/messages.zig");
const ui = @import("../ui.zig");
const Session = @import("../session.zig").Session;
const Action = @import("../commands.zig").Action;

// Transforms (crop / rotate / filter / layout, all undoable) update the session's STRUCTURED edit
// state, so the exact edit serializes to a browser-compatible layout and shows live in GUI editors.

/// Map a /filter argument ("bw"|"sepia"|"invert"|"contour"|"none"|<colour>) onto the layout filter
/// and apply it; the named modes are checked before the colour fallback. False for an unknown one.
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
    } else if (core.parseColor(core.zstr(arg) orelse "")) |col| {
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
        logo.print(msg.exec_usage, .{});
        return false;
    }
    return runAction(session, io, commands.parseAction(arg));
}

/// Run one transform. True only when a new edit state was actually recorded — the usage, no-image,
/// bad-argument and full-turn paths mutate nothing, so the caller queues no sync upload for them.
pub fn runAction(session: *Session, io: std.Io, action: Action) bool {
    // A bare transform lists its variants / usage instead of acting (no image needed) — so
    // `/crop` never silently records a full-image crop and `/filter` shows what it takes.
    if (action.arg.len == 0) switch (action.kind) {
        .crop => {
            logo.print(msg.crop_usage, .{});
            logo.print(msg.crop_usage_example, .{});
            return false;
        },
        .rotate => {
            logo.print(msg.rotate_usage, .{});
            return false;
        },
        .filter => {
            ui.listFilters();
            return false;
        },
        .layout => {
            logo.err(msg.apply_needs_path, .{});
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
            const rect = pipeline.resolveCropSpec(cur.width, cur.height, spec, album) orelse return false;
            session.applyCrop(rect) catch return false;
            ui.ack(session, "cropped");
        },
        .rotate => {
            const n = std.fmt.parseInt(i32, action.arg, 10) catch {
                logo.err(msg.rotate_needs_int, .{});
                return false;
            };
            if (@mod(n, 4) == 0) {
                logo.print(msg.rotate_full_turn, .{n});
                return false;
            }
            session.applyRotate(n) catch return false;
            ui.ack(session, "rotated");
        },
        .filter => {
            if (!applyFilterArg(session, action.arg)) {
                logo.err(msg.unknown_filter, .{action.arg});
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

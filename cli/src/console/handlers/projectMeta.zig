//! `/project-color`, `/blank-color` and `/project-description`: the active server project's
//! metadata fields, each PUT version-guarded with a 409 re-read-and-retry.
const std = @import("std");
const image = @import("../../image.zig");
const server = @import("../../serverClient.zig");
const logo = @import("../../logo.zig");
const core = @import("../../core.zig");
const theme = @import("../../theme.zig");
const project = @import("../../project.zig");
const msg = @import("../../messages.zig");
const ui = @import("../ui.zig");
const Session = @import("../session.zig").Session;

/// The connected client + id for the active server project, or null (error printed) when
/// none is active or its server isn't connected. Shared by `/project-*`, `/rename`, `/expire`.
pub const ActiveProject = struct { client: *server.Client, id: []const u8 };
pub fn requireActiveProject(session: *Session) ?ActiveProject {
    if (!session.hasRemote()) {
        logo.err(msg.no_active_server_project, .{});
        return null;
    }
    const client = session.findServer(session.remote_url.?) orelse {
        logo.err(msg.project_server_disconnected, .{});
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
            logo.err(msg.could_not_read_project_color, .{@errorName(e)});
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
        const col = core.parseColor(core.zstr(trimmed) orelse "") orelse {
            logo.err(msg.invalid_color_or_clear, .{trimmed});
            return;
        };
        color = std.fmt.bufPrint(&hexbuf, "#{x:0>2}{x:0>2}{x:0>2}", .{ col.r, col.g, col.b }) catch "#000000";
    }

    if (!putProjectField(session, client, id, color, .color)) return; // message already printed
    session.setRemoteColor(color) catch {}; // so the status header repaints the name in it
    if (color.len == 0) {
        logo.print(msg.project_color_cleared, .{});
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
        logo.err(msg.could_not_read_blank_color, .{@errorName(e)});
        return;
    };
    defer session.gpa.free(cur);

    if (trimmed.len == 0) {
        if (cur.len == 0) {
            logo.print(msg.blank_color_not_blank, .{});
        } else {
            printProjectColor("blank colour", cur);
        }
        return;
    }
    if (cur.len == 0) {
        logo.err(msg.not_a_blank_project, .{});
        return;
    }
    var hexbuf: [8]u8 = undefined;
    const col = core.parseColor(core.zstr(trimmed) orelse "") orelse {
        logo.err(msg.invalid_color, .{trimmed});
        return;
    };
    const color = std.fmt.bufPrint(&hexbuf, "#{x:0>2}{x:0>2}{x:0>2}", .{ col.r, col.g, col.b }) catch "#000000";
    if (!putProjectField(session, client, id, color, .blank_color)) return; // message already printed
    printProjectColor("blank colour set to", color);
}

/// `/project-description [<text...>]` — set (bare clears) the active project's description,
/// shown as `/projects`' trailing note. Taken verbatim; a ~2000-char soft cap guards it.
pub fn doProjectDescription(session: *Session, arg: []const u8) !void {
    const active = requireActiveProject(session) orelse return;
    const client = active.client;
    const id = active.id;
    const trimmed = std.mem.trim(u8, arg, " \t");
    if (trimmed.len > 2000) {
        logo.err(msg.description_too_long, .{trimmed.len});
        return;
    }

    if (!putProjectField(session, client, id, trimmed, .description)) return; // message already printed
    if (trimmed.len == 0) {
        logo.print(msg.project_description_cleared, .{});
    } else {
        logo.print(msg.project_description_set, .{});
    }
}

/// A reset keyword for `/project-color`: clears the custom colour back to the theme accent.
fn isClearWord(s: []const u8) bool {
    const eq = std.ascii.eqlIgnoreCase;
    return eq(s, "clear") or eq(s, "none") or eq(s, "default");
}

pub const ProjectField = enum { color, name, blank_color, description };

/// Version-guarded PUT of one project metadata field with a 409 re-read-and-retry (a peer
/// saved first), mirroring pushLayout; advances the LWW guard so our own echo isn't taken
/// for a peer edit. Returns success; prints on a hard failure.
pub fn putProjectField(session: *Session, client: *server.Client, id: []const u8, value: []const u8, field: ProjectField) bool {
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
                .color => logo.err(msg.could_not_set_project_color, .{@errorName(e)}),
                .name => logo.err(msg.could_not_rename_project, .{@errorName(e)}),
                .blank_color => logo.err(msg.could_not_set_blank_color, .{@errorName(e)}),
                .description => logo.err(msg.could_not_set_description, .{@errorName(e)}),
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

/// Print "<label>: <colour>" with the colour rendered in itself (truecolor) when colour is on
/// and the hex parses; an empty colour reads as "(none — neutral grey)".
fn printProjectColor(label: []const u8, color: []const u8) void {
    if (color.len == 0) {
        logo.print(msg.color_none, .{label});
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

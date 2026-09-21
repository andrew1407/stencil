//! `/save`, `/layout` and `/delete`: writing the session out (locally, to the active server
//! project, or as a portable `.stencil` bundle) and removing a local project file. The
//! routing and the delete guards are pure so they are unit-tested without I/O.
const std = @import("std");
const pipeline = @import("../../pipeline.zig");
const net = @import("../../net.zig");
const server = @import("../../server/client.zig");
const logo = @import("../../app/logo.zig");
const commands = @import("../commands.zig");
const project = @import("../../project.zig");
const msg = @import("../../app/messages.zig");
const ui = @import("../ui.zig");
const Session = @import("../session.zig").Session;
const remoteEvents = @import("../remoteEvents.zig");

/// Where a `/save` should write. Pure so the routing is unit-tested without I/O.
pub const SaveTarget = enum { local, server, none };

/// `/save <path>` writes locally; a bare `/save` pushes the result to the active server project (the
/// manual counterpart to `/sync`), and is an error when there is no active project.
pub fn saveTarget(arg_len: usize, has_remote: bool) SaveTarget {
    if (arg_len != 0) return .local;
    if (has_remote) return .server;
    return .none;
}

pub fn doSave(session: *Session, io: std.Io, arg: []const u8) !void {
    if (!session.hasImage()) return ui.noImage();
    switch (saveTarget(arg.len, session.hasRemote())) {
        .none => logo.err(msg.save_needs_path, .{}),
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

/// `/layout [path]` — export the layout JSON (`/apply` *draws* one). A `.json` path is exact;
/// another is a directory getting "<path>/<project>.json"; bare writes it in the cwd.
pub fn doLayout(session: *Session, io: std.Io, arg: []const u8) !void {
    if (!session.hasImage()) return ui.noImage();
    const json = try session.currentLayoutJson();
    defer session.gpa.free(json);
    const name = commands.projectBaseName(session.label orelse "layout");
    const path = try commands.layoutTarget(session.gpa, arg, name);
    defer session.gpa.free(path);
    std.Io.Dir.cwd().writeFile(io, .{ .sub_path = path, .data = json }) catch |e| {
        logo.err(msg.could_not_write_layout, .{ path, @errorName(e) });
        return;
    };
    logo.print(msg.wrote_layout, .{path});
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

/// `/delete <file>.stencil` (aliases `del`/`remove`/`rm`) — delete a local project file from
/// disk (the browser/desktop trash button). The session is untouched; the guards keep it narrow.
pub fn doDelete(io: std.Io, arg: []const u8) !void {
    switch (deleteReject(arg)) {
        .ok => {},
        .empty => return logo.err(msg.delete_needs_path, .{}),
        .url => return logo.err(msg.delete_no_urls, .{}),
        .not_stencil => return logo.err(msg.delete_not_stencil, .{arg}),
        .traversal => return logo.err(msg.delete_escapes_cwd, .{arg}),
    }
    std.Io.Dir.cwd().deleteFile(io, arg) catch |e|
        return logo.err(msg.could_not_delete, .{ arg, @errorName(e) });
    logo.print(msg.deleted, .{arg});
}

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

test "deleteReject: empty/url/non-stencil/traversal guards gate a local .stencil delete" {
    try testing.expectEqual(DeleteReject.empty, deleteReject(""));
    try testing.expectEqual(DeleteReject.url, deleteReject("https://x/a.stencil"));
    try testing.expectEqual(DeleteReject.not_stencil, deleteReject("notes.txt"));
    try testing.expectEqual(DeleteReject.traversal, deleteReject("../up.stencil"));
    try testing.expectEqual(DeleteReject.traversal, deleteReject("sub/../../up.stencil"));
    try testing.expectEqual(DeleteReject.ok, deleteReject("project.stencil"));
    try testing.expectEqual(DeleteReject.ok, deleteReject("sub/dir/project.stencil"));
}

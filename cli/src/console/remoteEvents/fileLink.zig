//! The linked `.stencil` file `/desktopsync` shares with the desktop app: each recorded edit
//! writes it at the prompt boundary, and a change the desktop wrote is adopted as one undo step
//! (a new picture reloads the session). `linkAction` is the pure decision between the two.
const std = @import("std");
const logo = @import("../../app/logo.zig");
const msg = @import("../../app/messages.zig");
const commands = @import("../commands.zig");
const project = @import("../../project.zig");
const codec = @import("../../project/codec.zig");
const session_mod = @import("../session.zig");
const ui = @import("../ui.zig");

const Session = session_mod.Session;

pub const FileLink = struct {
    path: []u8,
    mtime_ns: i128 = 0,
    size: u64 = 0,
    dirty: bool = false,

    pub fn deinit(self: *FileLink, gpa: std.mem.Allocator) void {
        gpa.free(self.path);
        self.* = undefined;
    }
};

pub const LinkAction = enum { none, save, reload };

/// Our pending edits win: they are written, and a disk change meanwhile is only noted.
pub fn linkAction(linked: bool, dirty: bool, changed_on_disk: bool) LinkAction {
    if (!linked) return .none;
    if (dirty) return .save;
    return if (changed_on_disk) .reload else .none;
}

/// Link the session to `path`, remembering how the file looks now.
pub fn link(session: *Session, io: std.Io, path: []const u8) !void {
    unlink(session);
    var l = FileLink{ .path = try session.gpa.dupe(u8, path) };
    stat(&l, io);
    session.file_link = l;
}

pub fn unlink(session: *Session) void {
    if (session.file_link) |*l| l.deinit(session.gpa);
    session.file_link = null;
}

/// A recorded edit: the file is written at the next prompt boundary.
pub fn markDirty(session: *Session) void {
    if (session.file_link) |*l| l.dirty = true;
}

/// The prompt-boundary tick: write our edits, or adopt the desktop's. True when something moved.
pub fn poll(session: *Session, io: std.Io) bool {
    const l = &(session.file_link orelse return false);
    const now = std.Io.Dir.cwd().statFile(io, l.path, .{}) catch {
        logo.print(msg.filelink_gone, .{l.path});
        unlink(session);
        return true;
    };
    const changed = now.mtime.nanoseconds != l.mtime_ns or now.size != l.size;
    switch (linkAction(true, l.dirty, changed)) {
        .none => return false,
        .save => {
            if (changed) logo.note(msg.filelink_conflict, .{l.path});
            if (!session.hasImage()) return false;
            project.saveInto(session, io, l.path, .{
                .name = commands.projectBaseName(session.label orelse "project"),
                .color = session.remote_color orelse "",
            }) catch return false;
            l.dirty = false;
            stat(l, io);
            return true;
        },
        .reload => {
            reload(session, io, l) catch |e| logo.print(msg.filelink_reload_failed, .{ l.path, @errorName(e) });
            stat(l, io);
            return true;
        },
    }
}

fn stat(l: *FileLink, io: std.Io) void {
    const st = std.Io.Dir.cwd().statFile(io, l.path, .{}) catch return;
    l.mtime_ns = st.mtime.nanoseconds;
    l.size = st.size;
}

// The same picture: the file's layout becomes one history step, so `/undo` takes it back.
// Another picture: the whole project loads afresh, as a peer's new original does.
fn reload(session: *Session, io: std.Io, l: *FileLink) !void {
    const bytes = try std.Io.Dir.cwd().readFileAlloc(io, l.path, session.gpa, .limited(64 << 20));
    defer session.gpa.free(bytes);
    var proj = try codec.parse(session.gpa, bytes);
    defer proj.deinit();
    const same = session.hasImage() and session.source_bytes != null and std.mem.eql(u8, session.source_bytes.?, proj.image_bytes);
    if (same) {
        var st = session_mod.EditState{};
        session_mod.parseLayoutInto(session.gpa, proj.layout_json, &st) catch {};
        try session.pushState(st);
        session.adoptLayoutMeta(proj.layout_json);
        try session.rebuild();
    } else {
        var loaded = try project.loadInto(session, io, l.path);
        loaded.deinit();
    }
    ui.redraw(session);
    logo.print(msg.filelink_reloaded, .{l.path});
}

const testing = std.testing;

test "linkAction: our edits are written first; a disk change alone reloads" {
    try testing.expectEqual(LinkAction.none, linkAction(false, true, true));
    try testing.expectEqual(LinkAction.none, linkAction(true, false, false));
    try testing.expectEqual(LinkAction.save, linkAction(true, true, false));
    try testing.expectEqual(LinkAction.save, linkAction(true, true, true));
    try testing.expectEqual(LinkAction.reload, linkAction(true, false, true));
}

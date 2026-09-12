//! The op-plan actions that change the SESSION, not the picture (contract §10): undo/redo/
//! reset, the server pool, the accent, opening a file or URL, clearing, and copying.
const std = @import("std");
const image = @import("../../image.zig");
const pipeline = @import("../../pipeline.zig");
const logo = @import("../../logo.zig");
const llm = @import("../../llm.zig");
const layout_mod = @import("../../layout.zig");
const project = @import("../../project.zig");
const ui = @import("../ui.zig");
const Session = @import("../session.zig").Session;
const handlers = @import("../handlers.zig");
const attachments = @import("../attachments.zig");
const planOps = @import("planOps.zig");
const planStep = planOps.planStep;
const planConnect = planOps.planConnect;
const planDisconnect = planOps.planDisconnect;
const planReconnect = planOps.planReconnect;

/// The console-settings half of one op-plan action (the §10-analog profile): history
/// steps, the server pool, the theme, local files and the clipboard. None of them edits
/// the picture, and every miss is a printed note rather than a failed plan.
pub fn apply(
    session: *Session,
    io: std.Io,
    a: llm.Action,
    edited: *bool,
    frame_steps: *std.ArrayList(layout_mod.FrameStep),
    active: *?usize,
) bool {
    const gpa = session.gpa;
    switch (a) {
        // §2 undo/redo/reset (top-level only): the console's OWN history, through the
        // same /undo, /redo and /reset paths — steps running out is a note, never a
        // failed plan (contract §2). Like the commands, they queue no sync of their own.
        .undo => |u| planStep(session, false, u.steps),
        .redo => |r| planStep(session, true, r.steps),
        .reset => handlers.doReset(session), // "no image loaded" is its own note
        // Console-settings ops (the §10-analog profile): they run the SAME handlers
        // the /theme, /connect, /disconnect, /reconnect, /delete and /drop commands use,
        // and every miss is a printed note, never a failed plan. None of them edits the
        // image.
        // A preset rides the same /theme path as a hex — its name table resolves it,
        // and an unknown name is /theme's own note + skip (§10).
        .accent => |c| handlers.doTheme(session, if (c.preset.len != 0) c.preset else c.color),
        .connect => |c| planConnect(session, io, c.server),
        .disconnect => |c| planDisconnect(session, c.server),
        .reconnect => |c| planReconnect(session, io, c.server),
        .delete => |d| handlers.doDelete(io, d.path) catch {}, // same guards + messages as /delete
        // §10 clear: drop the working image + its lines — the console's /drop (an empty
        // console is its own note when nothing is loaded).
        .clear => handlers.doDrop(session),
        // §10 clearChat: only ARM the deferred clear — the confirm and the actual
        // /chat-clear run once the WHOLE /prompt turn settles (finishChatClear).
        .clear_chat => session.pending_chat_clear = true,
        // §10 openFile (user-echo pre-checked by runPlan): the same load /upload performs —
        // .stencil restores a project, .json draws its layout, anything else is a picture
        // (or a video's first frame).
        .open_file => |f| {
            const path = pipeline.expandHome(gpa, f.path) catch return false;
            defer gpa.free(path);
            if (project.isStencilPath(path)) {
                handlers.openProject(session, io, path) catch return false;
                frame_steps.clearRetainingCapacity();
                active.* = null;
                return true;
            }
            if (std.ascii.endsWithIgnoreCase(path, ".json")) {
                const bytes = pipeline.loadLayoutBytes(gpa, io, path) catch return true; // message printed
                defer gpa.free(bytes);
                session.addLines(bytes) catch return false;
                ui.redraw(session);
                return true;
            }
            const src = pipeline.acquireInput(gpa, io, path, 0) catch return true; // message printed
            session.loadImage(src.img, path, false, src.default_fmt, src.bytes) catch return false;
            frame_steps.clearRetainingCapacity(); // a fresh picture = a fresh coordinate frame
            active.* = null;
            ui.redraw(session);
        },
        // §10 openUrl (user-echo pre-checked by runPlan): the same load /upload <url>
        // performs, synchronous — later actions see the fetched picture, and an unnamed
        // save derives its name from the URL label, like an upload's.
        .open_url => |o| {
            if (o.incognito) logo.note("incognito is not a console concept — loading normally\n", .{});
            const src = pipeline.acquireInput(gpa, io, o.url, 0) catch return false; // message printed
            session.loadImage(src.img, o.url, true, src.default_fmt, src.bytes) catch return false;
            frame_steps.clearRetainingCapacity(); // a fresh picture = a fresh coordinate frame
            active.* = null; // the loaded URL, not an earlier attachment, names a save now
            edited.* = true;
            ui.redraw(session);
        },
        // §10 copy: the console's /copy. No working image is a note + skip, never a stop.
        .copy => {
            if (!session.hasImage()) {
                logo.note("skipped copy — no working image to copy\n", .{});
                return true;
            }
            attachments.doCopy(session, io) catch return false;
        },
        .frame => return false, // pre-checked by runPlan (plan-level error)
        else => unreachable, // plan.zig routes the editing ops to edits.zig
    }
    return true;
}

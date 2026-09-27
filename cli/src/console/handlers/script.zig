//! `/script <text>` and `/script-run <file>` — run a .stc against the loaded image. The
//! console owns a session, not a file, so a `@source` block is reported and skipped: the
//! ops still apply to what is open, which is what the user is looking at.
const std = @import("std");

const logo = @import("../../app/logo.zig");
const msg = @import("../../app/messages.zig");
const pipeline = @import("../../pipeline.zig");
const script_mod = @import("../../script.zig");
const scriptCore = @import("../../script/core.zig");
const ui = @import("../ui.zig");
const Session = @import("../session.zig").Session;
const Log = @import("../session/scriptLog.zig").Log;

const decode = script_mod.decode;
const script_load = script_mod.load;

/// What the console cannot honour: it owns a session, not files. Reported once per kind
/// rather than skipped in silence (stc-contract §10).
const Skipped = struct {
    save: bool = false,
    frame: bool = false,

    fn note(self: *Skipped, kind: scriptCore.OpKind) void {
        switch (kind) {
            .save => if (!self.save) {
                self.save = true;
                logo.note(msg.script_save_ignored, .{});
            },
            .frame => if (!self.frame) {
                self.frame = true;
                logo.note(msg.script_frame_ignored, .{});
            },
            else => {},
        }
    }
};

const Unresolved = error{Unresolved};

/// Apply the script's ops in order through the run's log, returning how many edits survive. An
/// op whose lengths resolve to nothing, or a layout that cannot be read, stops the run
/// (stc-contract §10: reported, never skipped); `at` is then that op.
fn applyOps(session: *Session, io: std.Io, script: scriptCore.Script, log: *Log, at: *u32) !usize {
    const gpa = session.gpa;
    var skipped: Skipped = .{};
    var i: u32 = 0;
    while (i < script.opCount()) : (i += 1) {
        at.* = i;
        const op = script.op(i) orelse continue;
        const view = session.current();
        var buf: scriptCore.ResolveBuf = undefined;
        const edit = decode.decode(script, i, op.kind, @floatFromInt(view.width), @floatFromInt(view.height), &buf) orelse return Unresolved.Unresolved;
        switch (edit) {
            .crop => |rect| try log.record(session, .{ .crop = rect }),
            .filter => |f| {
                const mode = try gpa.dupe(u8, f.mode);
                const tint = gpa.dupe(u8, f.tint) catch |e| {
                    gpa.free(mode);
                    return e;
                };
                try log.record(session, .{ .filter = .{ .mode = mode, .tint = tint } });
            },
            .shape => |line| try log.shape(session, line),
            .layout => |l| try log.record(session, .{ .layout = .{
                .doc = pipeline.loadLayoutBytes(gpa, io, l.src) catch return Unresolved.Unresolved,
                .replace = std.mem.eql(u8, l.mode, "replace"),
            } }),
            .steps => |n| try log.undo(session, n),
            .none => {},
            else => skipped.note(op.kind),
        }
    }
    try log.finish(session);
    return log.entries.items.len;
}

/// Runs `source` against the session. Returns true when it recorded an edit.
fn runSource(session: *Session, io: std.Io, source: []const u8, label: []const u8) bool {
    var script = scriptCore.Script.parse(source) catch {
        logo.err(msg.script_unreadable, .{});
        return false;
    };
    defer script.deinit();

    if (script_load.reportDiagnostics(script, label)) return false;
    if (!session.hasImage()) {
        ui.noImage();
        return false;
    }

    var b: u32 = 0;
    while (b < script.blockCount()) : (b += 1) {
        const block = script.block(b) orelse continue;
        if (block.kind != .project) {
            logo.note(msg.script_source_ignored, .{});
            break;
        }
    }

    // One state for the whole run, so `/undo` takes the script back in one step.
    session.run_floor = session.cursor;
    defer session.run_floor = null;
    var log = Log{ .gpa = session.gpa };
    defer log.deinit();
    var at: u32 = 0;
    const applied = applyOps(session, io, script, &log, &at) catch |e| {
        // All or nothing: what the run recorded goes, and the op that stopped it is named.
        session.cursor = session.run_floor.?;
        session.dropAfterCursor();
        session.rebuild() catch {};
        const kind = if (script.op(at)) |op| @tagName(op.kind) else "?";
        if (e == Unresolved.Unresolved) logo.err(msg.script_op_unresolved, .{ at + 1, kind }) else logo.err(msg.script_failed, .{});
        return false;
    };
    if (applied == 0) {
        logo.print(msg.script_no_edits, .{});
        return false;
    }
    ui.redraw(session);
    return true;
}

/// `/script <text…>` — the script itself on the command line; `;` separates statements.
pub fn doScript(session: *Session, io: std.Io, arg: []const u8) bool {
    if (std.mem.trim(u8, arg, " \t").len == 0) {
        logo.print(msg.script_usage, .{});
        return false;
    }
    return runSource(session, io, arg, "<console>");
}

/// `/script-run <file.stc>` — the same, read from a file.
pub fn doScriptRun(session: *Session, io: std.Io, arg: []const u8) bool {
    const path = std.mem.trim(u8, arg, " \t");
    if (path.len == 0) {
        logo.print(msg.script_run_needs_path, .{});
        return false;
    }
    const source = script_load.readScript(session.gpa, io, path) catch {
        logo.err(msg.script_unreadable, .{});
        return false;
    };
    defer session.gpa.free(source);
    return runSource(session, io, source, path);
}

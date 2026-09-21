//! `/script <text>` and `/script-run <file>` — run a .stc against the loaded image. The
//! console owns a session, not a file, so a `@source` block is reported and skipped: the
//! ops still apply to what is open, which is what the user is looking at.
const std = @import("std");

const core = @import("../../core.zig");
const logo = @import("../../app/logo.zig");
const msg = @import("../../app/messages.zig");
const pipeline = @import("../../pipeline.zig");
const script_mod = @import("../../script.zig");
const scriptCore = @import("../../script/core.zig");
const ui = @import("../ui.zig");
const Session = @import("../session.zig").Session;

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

fn applyOps(session: *Session, io: std.Io, script: scriptCore.Script) !usize {
    var applied: usize = 0;
    var skipped: Skipped = .{};

    var i: u32 = 0;
    while (i < script.opCount()) : (i += 1) {
        const op = script.op(i) orelse continue;
        const view = session.current();
        var buf: scriptCore.ResolveBuf = undefined;
        const edit = decode.decode(script, i, op.kind, @floatFromInt(view.width), @floatFromInt(view.height), &buf) orelse continue;

        switch (edit) {
            .crop => |rect| {
                try session.applyCrop(rect);
                applied += 1;
            },
            .filter => |f| {
                try session.setFilter(f.mode, f.tint);
                applied += 1;
            },
            // One `addLines` per shape, never batched: §7 numbers every edit and the
            // console's `@undo N` walks that history N single steps.
            .shape => |line| {
                const doc = try layoutDoc(session.gpa, line, view.width, view.height);
                defer session.gpa.free(doc);
                try session.addLines(doc);
                applied += 1;
            },
            .layout => |l| {
                const bytes = pipeline.loadLayoutBytes(session.gpa, io, l.src) catch continue;
                defer session.gpa.free(bytes);
                try session.addLines(bytes);
                applied += 1;
            },
            .steps => |n| {
                var left = n;
                while (left > 0) : (left -= 1) _ = if (op.kind == .undo) session.undo() else session.redo();
            },
            else => skipped.note(op.kind),
        }
    }
    return applied;
}

/// One resolved shape as the single-line layout document `addLines` merges. Written straight
/// into the buffer that is handed on — nothing is copied out of a second writer.
fn layoutDoc(gpa: std.mem.Allocator, line: core.LineDraw, w: usize, h: usize) ![]u8 {
    var aw: std.Io.Writer.Allocating = .init(gpa);
    errdefer aw.deinit();
    const p = &aw.writer;
    try p.print("{{\"imageWidth\":{d},\"imageHeight\":{d},\"lines\":[{{\"color\":\"{s}\"," ++
        "\"style\":\"{s}\",\"fillColor\":\"{s}\",\"pointColor\":\"{s}\",\"thickness\":{d}," ++
        "\"pointSize\":{d},\"locked\":{s},\"points\":[", .{
        w,                                    h,
        line.color,                           line.style,
        line.fill_color,                      line.point_color,
        line.thickness,                       line.point_size,
        if (line.locked) "true" else "false",
    });
    var k: usize = 0;
    while (k + 1 < line.points.len) : (k += 2) {
        if (k > 0) try p.writeAll(",");
        try p.print("{{\"x\":{d},\"y\":{d}}}", .{ line.points[k], line.points[k + 1] });
    }
    try p.writeAll("]}]}");
    return aw.toOwnedSlice();
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

    const applied = applyOps(session, io, script) catch {
        logo.err(msg.script_failed, .{});
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

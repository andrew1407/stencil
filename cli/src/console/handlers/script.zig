//! `/script <text>` and `/script-run <file>` — run a .stc against the loaded image. The
//! console owns a session, not a file, so a `@source` block is reported and skipped: the
//! ops still apply to what is open, which is what the user is looking at.
const std = @import("std");

const logo = @import("../../logo.zig");
const msg = @import("../../messages.zig");
const pipeline = @import("../../pipeline.zig");
const scriptCore = @import("../../scriptCore.zig");
const script_load = @import("../../script/load.zig");
const ui = @import("../ui.zig");
const Session = @import("../session.zig").Session;

/// CSS pixels per cm at 96 dpi, the basis the crop parser and the browser share.
const PX_PER_CM: f64 = 96.0 / 2.54;

fn applyOps(session: *Session, io: std.Io, script: scriptCore.Script) !usize {
    var applied: usize = 0;
    var lines: std.ArrayList(u8) = .empty;
    defer lines.deinit(session.gpa);

    var i: u32 = 0;
    while (i < script.opCount()) : (i += 1) {
        const op = script.op(i) orelse continue;
        const view = session.current();
        const w: f64 = @floatFromInt(view.width);
        const h: f64 = @floatFromInt(view.height);
        var buf: [416]f64 = undefined;

        switch (op.kind) {
            .crop => {
                const r = script.resolve(i, w, h, PX_PER_CM, PX_PER_CM, &buf) catch continue;
                if (r.len < 4) continue;
                try session.applyCrop(.{
                    .x = @intFromFloat(@round(r[0])),
                    .y = @intFromFloat(@round(r[1])),
                    .w = @intFromFloat(@round(r[2])),
                    .h = @intFromFloat(@round(r[3])),
                });
                applied += 1;
            },
            .filter => {
                const mode = script.opStr(i, 0);
                const tint = script.opStr(i, 1);
                try session.setFilter(mode, tint);
                applied += 1;
            },
            .line, .rect => {
                const r = script.resolve(i, w, h, PX_PER_CM, PX_PER_CM, &buf) catch continue;
                if (r.len < 4) continue;
                lines.clearRetainingCapacity();
                try appendLayout(session.gpa, &lines, script, i, r, op.kind == .rect, view.width, view.height);
                try session.addLines(lines.items);
                applied += 1;
            },
            .layout => {
                const bytes = pipeline.loadLayoutBytes(session.gpa, io, script.opStr(i, 0)) catch continue;
                defer session.gpa.free(bytes);
                try session.addLines(bytes);
                applied += 1;
            },
            .undo => {
                var n: usize = @intFromFloat(script.opNum(i, 0) orelse 1);
                while (n > 0) : (n -= 1) _ = session.undo();
            },
            .redo => {
                var n: usize = @intFromFloat(script.opNum(i, 0) orelse 1);
                while (n > 0) : (n -= 1) _ = session.redo();
            },
            else => {},
        }
    }
    return applied;
}

fn appendLayout(
    gpa: std.mem.Allocator,
    out: *std.ArrayList(u8),
    script: scriptCore.Script,
    i: u32,
    r: []const f64,
    locked: bool,
    w: usize,
    h: usize,
) !void {
    var aw: std.Io.Writer.Allocating = .init(gpa);
    defer aw.deinit();
    const p = &aw.writer;
    try p.print("{{\"imageWidth\":{d},\"imageHeight\":{d},\"lines\":[{{\"color\":\"{s}\"," ++
        "\"style\":\"{s}\",\"fillColor\":\"{s}\",\"pointColor\":\"{s}\",\"thickness\":{d}," ++
        "\"pointSize\":{d},\"locked\":{s},\"points\":[", .{
        w,                       h,
        script.opStr(i, 0),      script.opStr(i, 1),
        script.opStr(i, 2),      script.opStr(i, 3),
        r[r.len - 2],            r[r.len - 1],
        if (locked) "true" else "false",
    });
    var k: usize = 0;
    while (k + 1 < r.len - 2) : (k += 2) {
        if (k > 0) try p.writeAll(",");
        try p.print("{{\"x\":{d},\"y\":{d}}}", .{ r[k], r[k + 1] });
    }
    try p.writeAll("]}]}");
    try out.appendSlice(gpa, aw.written());
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

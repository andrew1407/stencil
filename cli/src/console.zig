//! Interactive console ("stream") mode, `--console` / `--repl`: read `/command <args>` lines from
//! stdin and apply them to one in-memory working image through pipeline.zig's transforms. This file
//! is the input loop and the verb dispatch; the pieces live in console/ — `session` (undo/redo),
//! `commands` (grammar), `ui` (presentation), `handlers`. On a TTY, line_edit.zig adds raw mode.
const std = @import("std");
const logo = @import("logo.zig");
const llm = @import("llm.zig");
const line_edit = @import("line_edit.zig");
const session_mod = @import("console/session.zig");
const commands = @import("console/commands.zig");
const ui = @import("console/ui.zig");
const handlers = @import("console/handlers.zig");
const attachments = @import("console/attachments.zig");
const remoteEvents = @import("console/remoteEvents.zig");
const llmPrompt = @import("console/llmPrompt.zig");
const screen = @import("console/screen.zig");
const clipboard = @import("clipboard.zig");

pub const Session = session_mod.Session;

const LINE_BUF = 64 * 1024; // piped-input line cap: long crop specs / URLs fit on one line
const hooks = @import("console/hooks.zig");
const loop = @import("console/loop.zig");
const dispatch_mod = @import("console/dispatch.zig");

pub const handle = dispatch_mod.handle;

const runInteractive = loop.runInteractive;
const runPiped = loop.runPiped;
const idleTick = hooks.idleTick;
const IdleCtx = hooks.IdleCtx;

pub fn run(gpa: std.mem.Allocator, io: std.Io, full_screen: bool, llm_env: llm.Env) !void {
    var session = Session{ .gpa = gpa, .llm_env = llm_env };
    defer session.deinit();

    const stdin_file = std.Io.File.stdin();
    const is_tty = stdin_file.isTty(io) catch false;
    var editor: ?line_edit.Editor = if (is_tty) (line_edit.Editor.init(stdin_file.handle) catch null) else null;
    if (editor) |*ed| {
        ui.setInteractive(true);
        defer ed.deinit();
        defer ui.setInteractive(false);
        // Full-screen mode (pinned logo header + scrollback + mouse) is opt-in via --console-full-screen,
        // falling back to the plain banner + line editor when the terminal is too small or unmeasurable.
        var scr = screen.Screen{ .gpa = gpa, .io = io };
        scr.in_fd = stdin_file.handle; // start() reads the terminal's colour answer on it
        const started = full_screen and (if (scr.start()) |_| true else |_| false);
        if (started) {
            defer scr.deinit();
            ed.screen = &scr;
            ed.io = io;
            ui.intro();
            ui.status(&session);
            runInteractive(gpa, io, &session, ed, &scr);
        } else {
            logo.banner();
            ui.intro();
            ui.status(&session);
            runInteractive(gpa, io, &session, ed, null);
        }
    } else {
        logo.banner();
        ui.intro();
        ui.status(&session);
        runPiped(io, &session);
    }
}

test {
    _ = @import("console/session.zig");
    _ = @import("console/commands.zig");
    _ = @import("console/ui.zig");
    _ = @import("console/handlers.zig");
    _ = @import("console/screen.zig");
    _ = @import("console/ansi.zig");
    _ = @import("console/logoFx.zig");
    _ = @import("console/projectsTable.zig");
    _ = @import("console/remoteEvents.zig");
    _ = @import("console/attachments.zig");
    _ = @import("console/spinner.zig");
    _ = @import("console/derivedView.zig");
    _ = @import("console/llmPrompt.zig");
    _ = hooks;
    _ = loop;
    _ = dispatch_mod;
}

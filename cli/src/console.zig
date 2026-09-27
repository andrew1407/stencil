//! Interactive console ("stream") mode, `--console` / `--repl`: read `/command <args>` lines from
//! stdin and apply them to one in-memory working image through pipeline.zig's transforms. This file
//! is the input loop and the verb dispatch; the pieces live in console/ — `session` (undo/redo),
//! `commands` (grammar), `ui` (presentation), `handlers`. On a TTY, line_edit.zig adds raw mode.
const std = @import("std");
const logo = @import("app/logo.zig");
const llm = @import("llm.zig");
const args = @import("args.zig");
const line_edit = @import("line_edit/line_edit.zig");
const session_mod = @import("console/session.zig");
const ui = @import("console/ui.zig");
const screen = @import("console/screen.zig");

pub const Session = session_mod.Session;

const hooks = @import("console/hooks.zig");
const loop = @import("console/loop.zig");
const dispatch_mod = @import("console/dispatch.zig");

pub const handle = dispatch_mod.handle;

const runInteractive = loop.runInteractive;
const runPiped = loop.runPiped;

pub fn run(gpa: std.mem.Allocator, io: std.Io, full_screen: bool, llm_env: llm.Env, server_tokens: args.EnvTokens) !void {
    var session = Session{ .gpa = gpa, .llm_env = llm_env, .server_tokens = server_tokens };
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
    _ = @import("console/render/ansi.zig");
    _ = @import("console/render/logoFx.zig");
    _ = @import("console/render/projectsTable.zig");
    _ = @import("console/render/inert.zig");
    _ = @import("console/netWait.zig");
    _ = @import("console/remoteEvents.zig");
    _ = @import("console/attachments.zig");
    _ = @import("console/render/spinner.zig");
    _ = @import("console/render/derivedView.zig");
    _ = @import("console/llmPrompt.zig");
    _ = hooks;
    _ = loop;
    _ = dispatch_mod;
}

//! Interactive console ("stream") mode, activated by `--console` / `--repl`: read
//! `/command <args>` lines from stdin and apply them to a single in-memory working image,
//! reusing pipeline.zig's transforms. This file is just the input loop and the verb/action
//! dispatch; the pieces live in console/ — `session` (undo/redo state), `commands` (grammar),
//! `ui` (presentation) and `handlers` (the command implementations). On a TTY, line_edit.zig
//! adds raw-mode line editing; piped input uses a plain reader.
const std = @import("std");
const logo = @import("logo.zig");
const line_edit = @import("line_edit.zig");
const session_mod = @import("console/session.zig");
const commands = @import("console/commands.zig");
const ui = @import("console/ui.zig");
const handlers = @import("console/handlers.zig");
const screen = @import("console/screen.zig");
const clipboard = @import("clipboard.zig");

pub const Session = session_mod.Session;

const LINE_BUF = 64 * 1024; // piped-input line cap: long crop specs / URLs fit on one line

pub fn run(gpa: std.mem.Allocator, io: std.Io, full_screen: bool) !void {
    var session = Session{ .gpa = gpa };
    defer session.deinit();

    const stdin_file = std.Io.File.stdin();
    const is_tty = stdin_file.isTty(io) catch false;
    var editor: ?line_edit.Editor = if (is_tty) (line_edit.Editor.init(stdin_file.handle) catch null) else null;
    if (editor) |*ed| {
        ui.setInteractive(true);
        defer ed.deinit();
        defer ui.setInteractive(false);
        // Full-screen mode (pinned logo header + scrollback + mouse) is opt-in via
        // --console-full-screen. If the terminal is too small or its size can't be read it
        // falls back to the plain banner + line editor. The logo banner is only printed inline
        // in the fallback — in screen mode it IS the header.
        var scr = screen.Screen{ .gpa = gpa, .io = io };
        const started = full_screen and (if (scr.start()) |_| true else |_| false);
        if (started) {
            defer scr.deinit();
            ed.screen = &scr;
            ed.io = io;
            scr.in_fd = stdin_file.handle; // lets the theme flourish abort when a click is queued
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

// Idle hook: while the user sits at the prompt, poll the live events feed (so a peer's
// name/colour change surfaces without a keystroke) and, in screen mode, re-measure the
// terminal so a resize repaints — no SIGWINCH handler needed.
const IdleCtx = struct { session: *Session, io: std.Io, screen: ?*screen.Screen = null };
fn idleTick(raw: *anyopaque) bool {
    const c: *IdleCtx = @ptrCast(@alignCast(raw));
    const resized = if (c.screen) |s| s.tick() else false;
    const evented = handlers.pollEvents(c.session, c.io);
    return resized or evented; // true → the line editor repaints the prompt
}

// Single-click on the pinned logo: advance the accent, mirroring the browser logo.
fn logoCycle(raw: *anyopaque) void {
    const c: *IdleCtx = @ptrCast(@alignCast(raw));
    handlers.cycleTheme(c.session);
}

// Double-click on the pinned logo: set a random custom colour outside the preset list. The
// click time seeds the RNG so each double-click lands on a different hue.
fn logoCustom(raw: *anyopaque) void {
    const c: *IdleCtx = @ptrCast(@alignCast(raw));
    const seed: u64 = @bitCast(std.Io.Clock.now(.awake, c.io).toMilliseconds());
    handlers.randomCustomTheme(c.session, seed);
}

// Ctrl-S in full-screen mode: copy the current visual selection to the clipboard and note it.
fn copySelection(raw: *anyopaque, text: []const u8) void {
    const c: *IdleCtx = @ptrCast(@alignCast(raw));
    clipboard.writeText(c.session.gpa, c.io, text) catch {
        logo.print("could not copy the selection to the clipboard\n", .{});
        return;
    };
    logo.print("copied {d} chars to the clipboard\n", .{text.len});
}

fn runInteractive(gpa: std.mem.Allocator, io: std.Io, session: *Session, ed: *line_edit.Editor, scr: ?*screen.Screen) void {
    var hist = line_edit.History{ .gpa = gpa };
    defer hist.deinit();
    var buf: [line_edit.max_line]u8 = undefined;
    var armed = false; // one Ctrl-C arms exit; a second one in a row confirms it
    var idle_ctx = IdleCtx{ .session = session, .io = io, .screen = scr };
    ed.idle_cb = idleTick;
    ed.idle_ctx = &idle_ctx;
    if (scr != null) {
        ed.logo_cycle_cb = logoCycle;
        ed.logo_custom_cb = logoCustom;
        ed.copy_text_cb = copySelection;
        ed.logo_ctx = &idle_ctx;
    }
    while (true) {
        // A TTY read always blocks, so this is the burst-settled boundary: flush any
        // pending sync upload and surface any concurrent server edits before the prompt.
        handlers.flushSync(session, false);
        _ = handlers.pollEvents(session, io);
        switch (ed.readLine(ui.promptStr(session), &buf, &hist, &ui.completions, &armed, "")) {
            .eof => break, // Ctrl-D / closed tty
            .interrupt => { // Ctrl-C: require a second press in a row to leave
                if (armed) break;
                armed = true;
                logo.print("press Ctrl-C again to exit\n", .{});
            },
            .copy => { // Ctrl-Alt-C: copy the image to the clipboard
                if (session.hasImage()) handlers.doCopy(session, io) catch {};
            },
            .paste => { // Ctrl-Alt-V: load an image from the clipboard
                handlers.doPaste(session, io) catch |e| logo.print("error: {s}\n", .{@errorName(e)});
            },
            .line => |n| {
                const line = buf[0..n];
                hist.add(line);
                // In full-screen mode the prompt is a fixed row that gets cleared, so echo the
                // command into the scrollback first — otherwise its output has no visible source.
                if (scr != null and line.len != 0)
                    logo.print("{s}{s}{s}{s}\n", .{ logo.accentSeq(), ui.promptStr(session), logo.resetSeq(), line });
                if (!confirmUpload(ed, session, line)) continue; // guard /upload + /source-upload behind a yes/no prompt
                if (dispatch(session, io, line)) break;
            },
        }
    }
}

// On a TTY, `/upload <src>` (and `/source-upload <url>`) ask for a yes/no confirmation before
// replacing the working image. Returns true to proceed (always, for every other command);
// false when the user declines. Tests drive `handle` directly and so skip this prompt.
fn confirmUpload(ed: *line_edit.Editor, session: *Session, line: []const u8) bool {
    const cmd = commands.parseCommand(line);
    if (cmd.arg.len == 0) return true;
    const verb = commands.verbOf(cmd.word) orelse return true;
    var qbuf: [512]u8 = undefined;
    if (verb == commands.Verb.upload) {
        const q = std.fmt.bufPrint(&qbuf, "Upload {s}?", .{cmd.arg}) catch "Upload this source?";
        if (ed.confirm(q)) return true;
        logo.print("upload cancelled\n", .{});
        return false;
    }
    // A scrape replaces the working image too — but only prompt when there is one to replace.
    if (verb == commands.Verb.source_upload and session.hasImage()) {
        var it = std.mem.tokenizeAny(u8, cmd.arg, " \t");
        const url = it.next() orelse cmd.arg;
        const q = std.fmt.bufPrint(&qbuf, "Replace the current image with a scrape of {s}?", .{url}) catch "Replace the current image with a scrape?";
        if (ed.confirm(q)) return true;
        logo.print("scrape cancelled\n", .{});
        return false;
    }
    return true;
}

fn runPiped(io: std.Io, session: *Session) void {
    var buf: [LINE_BUF]u8 = undefined;
    var stdin = std.Io.File.stdin().readerStreaming(io, &buf);
    const r = &stdin.interface;
    while (true) {
        logo.print("{s}", .{ui.promptStr(session)});
        // takeDelimiter consumes the newline, returns the trailing unterminated line at EOF,
        // and null only at true end-of-stream (Ctrl-D / closed pipe).
        const maybe = r.takeDelimiter('\n') catch |e| switch (e) {
            error.StreamTooLong => {
                logo.print("error: input line too long (max {d} bytes)\n", .{LINE_BUF});
                _ = r.discardDelimiterInclusive('\n') catch {};
                continue;
            },
            error.ReadFailed => break,
        };
        const line = maybe orelse break;
        if (dispatch(session, io, line)) break;
        // Coalesce a piped burst: defer the sync upload while more commands are still
        // buffered, flushing once the reader's buffer drains (the burst has settled).
        handlers.flushSync(session, r.bufferedLen() != 0);
        _ = handlers.pollEvents(session, io);
    }
    handlers.flushSync(session, false); // final flush at end-of-stream
}

// Run one line; returns true when the session should end. Shared by both input loops.
fn dispatch(session: *Session, io: std.Io, line: []const u8) bool {
    return handle(session, io, line) catch |e| {
        logo.print("error: {s}\n", .{@errorName(e)});
        return false;
    };
}

/// Execute one command line against the session. Returns true when the session should end
/// (the `exit`/`quit` verbs). Exposed for the integration tests in tests/console_test.zig.
pub fn handle(session: *Session, io: std.Io, line: []const u8) !bool {
    const cmd = commands.parseCommand(line);
    if (cmd.word.len == 0) return false;
    if (commands.verbOf(cmd.word)) |verb| switch (verb) {
        .quit => return true,
        .help => ui.help(),
        .status => ui.status(session),
        .clear => ui.redraw(session),
        .upload => try handlers.doUpload(session, io, cmd.arg),
        .source_upload => try handlers.doSourceUpload(session, io, cmd.arg),
        .blank => try handlers.doBlank(session, cmd.arg),
        .save => try handlers.doSave(session, io, cmd.arg),
        .delete => try handlers.doDelete(io, cmd.arg),
        .layout => try handlers.doLayout(session, io, cmd.arg),
        // Formulas / the page format ride the layout, so a real change syncs to the server —
        // but the bare listing and rejected-argument paths mutate nothing and stay clean
        // (marking them dirty would upload an unchanged project and ping every peer).
        .formula => {
            if (handlers.doFormula(session, cmd.arg)) handlers.markDirty(session);
        },
        .format => {
            if (handlers.doFormat(session, cmd.arg)) handlers.markDirty(session);
        },
        .exec => {
            // Dirty only on a recorded edit; debounced, flushed at the prompt boundary.
            if (handlers.doExec(session, io, cmd.arg)) handlers.markDirty(session);
        },
        .undo => handlers.doStep(session, session.undo(), "undone", "nothing to undo (at the original)"),
        .redo => handlers.doStep(session, session.redo(), "redone", "nothing to redo (at the latest edit)"),
        .reset => handlers.doReset(session),
        .drop => handlers.doDrop(session),
        .copy => try handlers.doCopy(session, io),
        .paste => try handlers.doPaste(session, io),
        .theme => handlers.doTheme(session, cmd.arg),
        .mouse => handlers.doMouse(session, cmd.arg),
        .connect => try handlers.doConnect(session, io, cmd.arg),
        .disconnect => try handlers.doDisconnect(session, cmd.arg),
        .reconnect => try handlers.doReconnect(session, io, cmd.arg),
        .connections => handlers.doConnections(session),
        .projects => try handlers.doProjects(session, io, cmd.arg),
        .project_color => try handlers.doProjectColor(session, cmd.arg),
        .blank_color => try handlers.doProjectBlankColor(session, cmd.arg),
        .project_description => try handlers.doProjectDescription(session, cmd.arg),
        .keywords => try handlers.doKeywords(session, cmd.arg),
        .keywords_search => try handlers.doKeywordsSearch(session, cmd.arg),
        .keywords_add => try handlers.doKeywordsAdd(session, cmd.arg),
        .keywords_del => try handlers.doKeywordsDel(session, cmd.arg),
        .rename => try handlers.doRename(session, cmd.arg),
        .expire => try handlers.doExpire(session, io, cmd.arg),
        .fetch => try handlers.doFetch(session, io, cmd.arg),
        .sync => handlers.doSync(session, cmd.arg),
    } else if (commands.actionOf(cmd.word, cmd.arg)) |action| {
        // Dirty only on a recorded edit (usage/error paths change nothing); debounced,
        // flushed at the prompt boundary.
        if (handlers.runAction(session, io, action)) handlers.markDirty(session);
    } else {
        logo.print("error: unknown command '{s}' — type 'help' for the command list\n", .{cmd.word});
    }
    return false;
}

test {
    _ = @import("console/session.zig");
    _ = @import("console/commands.zig");
    _ = @import("console/ui.zig");
    _ = @import("console/handlers.zig");
    _ = @import("console/screen.zig");
}

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

pub const Session = session_mod.Session;

const LINE_BUF = 64 * 1024; // piped-input line cap: long crop specs / URLs fit on one line

pub fn run(gpa: std.mem.Allocator, io: std.Io) !void {
    var session = Session{ .gpa = gpa };
    defer session.deinit();

    logo.banner();
    ui.intro();
    ui.status(&session); // header line just under the logo

    const stdin_file = std.Io.File.stdin();
    const is_tty = stdin_file.isTty(io) catch false;
    var editor: ?line_edit.Editor = if (is_tty) (line_edit.Editor.init(stdin_file.handle) catch null) else null;
    if (editor) |*ed| {
        ui.setInteractive(true);
        defer ed.deinit();
        defer ui.setInteractive(false);
        runInteractive(gpa, io, &session, ed);
    } else {
        runPiped(io, &session);
    }
}

fn runInteractive(gpa: std.mem.Allocator, io: std.Io, session: *Session, ed: *line_edit.Editor) void {
    var hist = line_edit.History{ .gpa = gpa };
    defer hist.deinit();
    var buf: [line_edit.max_line]u8 = undefined;
    var armed = false; // one Ctrl-C copies + arms exit; a second one in a row confirms it
    while (true) {
        switch (ed.readLine(ui.promptStr(session), &buf, &hist, &ui.completions, &armed)) {
            .eof => break, // Ctrl-D / closed tty
            .interrupt => { // Ctrl-C: copy the image, then require a second press to leave
                if (armed) break;
                armed = true;
                if (session.hasImage()) handlers.doCopy(session, io) catch {};
                logo.print("press Ctrl-C again to exit\n", .{});
            },
            .paste => { // Ctrl-V: load an image from the clipboard
                handlers.doPaste(session, io) catch |e| logo.print("error: {s}\n", .{@errorName(e)});
            },
            .line => |n| {
                const line = buf[0..n];
                hist.add(line);
                if (!confirmUpload(ed, line)) continue; // guard /upload behind a yes/no prompt
                if (dispatch(session, io, line)) break;
            },
        }
    }
}

// On a TTY, `/upload <src>` asks for a yes/no confirmation before replacing the working
// image. Returns true to proceed (always, for every non-upload command); false when the
// user declines. Tests drive `handle` directly and so skip this prompt.
fn confirmUpload(ed: *line_edit.Editor, line: []const u8) bool {
    const cmd = commands.parseCommand(line);
    if (commands.verbOf(cmd.word) != commands.Verb.upload or cmd.arg.len == 0) return true;
    var qbuf: [512]u8 = undefined;
    const q = std.fmt.bufPrint(&qbuf, "Upload {s}?", .{cmd.arg}) catch "Upload this source?";
    if (ed.confirm(q)) return true;
    logo.print("upload cancelled\n", .{});
    return false;
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
    }
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
        .blank => try handlers.doBlank(session, cmd.arg),
        .save => try handlers.doSave(session, io, cmd.arg),
        .exec => try handlers.runAction(session, io, commands.parseAction(cmd.arg)),
        .undo => handlers.doStep(session, session.undo(), "undone", "nothing to undo (at the original)"),
        .redo => handlers.doStep(session, session.redo(), "redone", "nothing to redo (at the latest edit)"),
        .reset => handlers.doReset(session),
        .drop => handlers.doDrop(session),
        .copy => try handlers.doCopy(session, io),
        .paste => try handlers.doPaste(session, io),
        .theme => handlers.doTheme(session, cmd.arg),
    } else if (commands.actionOf(cmd.word, cmd.arg)) |action| {
        try handlers.runAction(session, io, action);
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
}

//! One command line against the session: the verb/action dispatch every input loop and the
//! integration tests go through.
const std = @import("std");
const logo = @import("../logo.zig");
const line_edit = @import("../line_edit.zig");
const clipboard = @import("../clipboard.zig");
const session_mod = @import("session.zig");
const commands = @import("commands.zig");
const ui = @import("ui.zig");
const handlers = @import("handlers.zig");
const attachments = @import("attachments.zig");
const remoteEvents = @import("remoteEvents.zig");
const llmPrompt = @import("llmPrompt.zig");
const screen = @import("screen.zig");

const Session = session_mod.Session;
const PipedConfirm = @import("loop.zig").PipedConfirm;

// Run one line; returns true when the session should end. Shared by both input loops.
pub fn dispatch(session: *Session, io: std.Io, line: []const u8) bool {
    return handle(session, io, line) catch |e| {
        logo.err("{s}\n", .{@errorName(e)});
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
            if (handlers.doFormula(session, cmd.arg)) remoteEvents.markDirty(session);
        },
        .format => {
            if (handlers.doFormat(session, cmd.arg)) remoteEvents.markDirty(session);
        },
        .exec => {
            // Dirty only on a recorded edit; debounced, flushed at the prompt boundary.
            if (handlers.doExec(session, io, cmd.arg)) remoteEvents.markDirty(session);
        },
        .undo => handlers.doStep(session, session.undo(), "undone", "nothing to undo (at the original)"),
        .redo => handlers.doStep(session, session.redo(), "redone", "nothing to redo (at the latest edit)"),
        .reset => handlers.doReset(session),
        .drop => handlers.doDrop(session),
        .copy => try attachments.doCopy(session, io),
        .paste => try attachments.doPaste(session, io),
        .unpaste => attachments.doUnpaste(session, cmd.arg),
        .images => attachments.doImages(session),
        .theme => handlers.doTheme(session, cmd.arg),
        .mouse => handlers.doMouse(session, cmd.arg),
        .reveal_speed => handlers.doRevealSpeed(session, cmd.arg),
        .connect => try handlers.doConnect(session, io, cmd.arg),
        .disconnect => try handlers.doDisconnect(session, cmd.arg),
        .reconnect => try handlers.doReconnect(session, io, cmd.arg),
        .connections => handlers.doConnections(session, cmd.arg),
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
        // LLM assistant: /prompt executes the returned op-plan through the same handlers
        // above (and queues its own sync marks); /llm shows/sets the provider config.
        .prompt => try llmPrompt.doPrompt(session, io, cmd.arg),
        .llm => try llmPrompt.doLlm(session, cmd.arg),
        .chat => handlers.doChat(session, cmd.arg),
    } else if (commands.actionOf(cmd.word, cmd.arg)) |action| {
        // Dirty only on a recorded edit (usage/error paths change nothing); debounced,
        // flushed at the prompt boundary.
        if (handlers.runAction(session, io, action)) remoteEvents.markDirty(session);
    } else {
        logo.err("unknown command '{s}' — type 'help' for the command list\n", .{cmd.word});
    }
    return false;
}

test "piped clearChat confirm: y/yes accepts; anything else — or EOF — declines" {
    const testing = std.testing;
    const Swallow = struct {
        fn sink(_: *anyopaque, _: []const u8) void {}
    };
    var dummy: u8 = 0;
    logo.setSink(Swallow.sink, &dummy);
    defer logo.clearSink();
    const cases = .{
        .{ "y\n", true },
        .{ " YES \n", true },
        .{ "yes", true }, // unterminated trailing line still answers
        .{ "n\n", false },
        .{ "yeah\n", false },
        .{ "\n", false },
        .{ "", false }, // EOF before any answer
    };
    inline for (cases) |case| {
        var r = std.Io.Reader.fixed(case[0]);
        var pc = PipedConfirm{ .r = &r };
        try testing.expectEqual(case[1], PipedConfirm.confirm(&pc, "Clear this conversation's chat history?"));
    }
}

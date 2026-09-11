//! The two input loops: raw-mode editing on a TTY (with its confirmations) and the plain
//! buffered reader for piped input. Both hand each line to dispatch.zig.
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
const hooks = @import("hooks.zig");
const dispatch_mod = @import("dispatch.zig");

const dispatch = dispatch_mod.dispatch;
const idleTick = hooks.idleTick;
const IdleCtx = hooks.IdleCtx;
const pollCancel = hooks.pollCancel;
const pendingPaste = hooks.pendingPaste;
const pendingAddPath = hooks.pendingAddPath;
const pendingKeep = hooks.pendingKeep;
const pendingCount = hooks.pendingCount;
const copySelection = hooks.copySelection;
const logoCycle = hooks.logoCycle;
const logoCustom = hooks.logoCustom;

const LINE_BUF = 64 * 1024; // piped-input line cap: long crop specs / URLs fit on one line

fn runInteractive(gpa: std.mem.Allocator, io: std.Io, session: *Session, ed: *line_edit.Editor, scr: ?*screen.Screen) void {
    var hist = line_edit.History{ .gpa = gpa };
    defer hist.deinit();
    // A deferred plan clearChat confirms through the same TTY keypress prompt /upload uses.
    session.confirm_fn = editorConfirm;
    session.confirm_ctx = ed;
    defer {
        session.confirm_fn = null;
        session.confirm_ctx = null;
    }
    var buf: [line_edit.max_line]u8 = undefined;
    var armed = false; // one Ctrl-C arms exit; a second one in a row confirms it
    var idle_ctx = IdleCtx{ .session = session, .io = io, .screen = scr };
    ed.idle_cb = idleTick;
    ed.idle_ctx = &idle_ctx;
    ed.pending = .{
        .ctx = &idle_ctx,
        .paste = pendingPaste,
        .addPath = pendingAddPath,
        .keep = pendingKeep,
        .count = pendingCount,
    };
    defer ed.pending = null;
    // Ctrl-C during a long call (an LLM turn) cancels it instead of queueing up as a
    // quit-later keystroke — the session polls this while it waits.
    session.cancel_ctx = ed;
    session.cancel_poll = pollCancel;
    defer {
        session.cancel_ctx = null;
        session.cancel_poll = null;
    }
    if (scr != null) {
        ed.logo_cycle_cb = logoCycle;
        ed.logo_custom_cb = logoCustom;
        ed.copy_text_cb = copySelection;
        ed.logo_ctx = &idle_ctx;
    }
    while (true) {
        // A TTY read always blocks, so this is the burst-settled boundary: flush any
        // pending sync upload and surface any concurrent server edits before the prompt.
        remoteEvents.flushSync(session, false);
        _ = remoteEvents.pollEvents(session, io);
        switch (ed.readLine(ui.promptStr(session), &buf, &hist, &ui.completions, &armed, "")) {
            .eof => break, // Ctrl-D / closed tty
            .interrupt => { // Ctrl-C: require a second press in a row to leave
                if (armed) break;
                armed = true;
                logo.print("press Ctrl-C again to exit\n", .{});
            },
            .copy => { // Ctrl-Alt-C: copy the image to the clipboard
                if (session.hasImage()) attachments.doCopy(session, io) catch {};
            },
            .paste => { // Ctrl-Alt-V: load an image from the clipboard
                attachments.doPaste(session, io) catch |e| logo.err("{s}\n", .{@errorName(e)});
            },
            .unpaste => attachments.doUnpaste(session, ""), // Ctrl-Z: take the last one back
            .line => |n| {
                // In full-screen mode the prompt is a fixed row that gets cleared, so echo the
                // command into the scrollback first — otherwise its output has no visible source.
                if (scr) |s| if (n != 0) {
                    s.skipRevealOnce(); // the echo is what you just typed, not output arriving
                    logo.print("{s}{s}{s}{s}\n", .{ logo.accentSeq(), ui.promptStr(session), logo.resetSeq(), buf[0..n] });
                };
                // Images pasted into the line ride it as `[Image #N …]` markers: lift them off
                // before the command is parsed — and before it is remembered, since a recalled
                // marker would name a picture that is long gone.
                var sbuf: [line_edit.max_line]u8 = undefined;
                const line = line_edit.stripMarkers(&sbuf, buf[0..n]);
                hist.add(line);
                if (attachments.drainPending(session, line)) continue; // the paste WAS the command
                if (!confirmUpload(ed, session, line)) continue; // guard /upload + /source-upload behind a yes/no prompt
                if (dispatch(session, io, line)) break;
            },
        }
    }
}

/// The session's cancel hook: the editor's tty watch, behind an opaque pointer so session.zig
/// keeps knowing nothing about the line editor.

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

// The in-app confirm a deferred plan clearChat shows (llm-contract §10): the same TTY
// keypress prompt the /upload guard uses. `ctx` is the interactive loop's line editor.
fn editorConfirm(ctx: ?*anyopaque, question: []const u8) bool {
    const ed: *line_edit.Editor = @ptrCast(@alignCast(ctx.?));
    return ed.confirm(question);
}

/// Piped mode's clearChat confirm: print the question and read ONE line from the SAME
/// buffered stdin reader the command loop uses (so a scripted "y" right after the
/// /prompt line is seen). EOF — or anything but y/yes — declines.
pub const PipedConfirm = struct {
    r: *std.Io.Reader,

    pub fn confirm(ctx: ?*anyopaque, question: []const u8) bool {
        const self: *PipedConfirm = @ptrCast(@alignCast(ctx.?));
        logo.print("{s} (y/N) ", .{question});
        const line = (self.r.takeDelimiter('\n') catch return false) orelse return false;
        const ans = std.mem.trim(u8, line, " \t\r");
        return std.ascii.eqlIgnoreCase(ans, "y") or std.ascii.eqlIgnoreCase(ans, "yes");
    }
};

pub fn runPiped(io: std.Io, session: *Session) void {
    var buf: [LINE_BUF]u8 = undefined;
    var stdin = std.Io.File.stdin().readerStreaming(io, &buf);
    const r = &stdin.interface;
    var pc = PipedConfirm{ .r = r };
    session.confirm_fn = PipedConfirm.confirm;
    session.confirm_ctx = &pc;
    defer {
        session.confirm_fn = null;
        session.confirm_ctx = null;
    }
    while (true) {
        logo.print("{s}", .{ui.promptStr(session)});
        // takeDelimiter consumes the newline, returns the trailing unterminated line at EOF,
        // and null only at true end-of-stream (Ctrl-D / closed pipe).
        const maybe = r.takeDelimiter('\n') catch |e| switch (e) {
            error.StreamTooLong => {
                logo.err("input line too long (max {d} bytes)\n", .{LINE_BUF});
                _ = r.discardDelimiterInclusive('\n') catch {};
                continue;
            },
            error.ReadFailed => break,
        };
        const line = maybe orelse break;
        if (dispatch(session, io, line)) break;
        // Coalesce a piped burst: defer the sync upload while more commands are still
        // buffered, flushing once the reader's buffer drains (the burst has settled).
        remoteEvents.flushSync(session, r.bufferedLen() != 0);
        _ = remoteEvents.pollEvents(session, io);
    }
    remoteEvents.flushSync(session, false); // final flush at end-of-stream
}

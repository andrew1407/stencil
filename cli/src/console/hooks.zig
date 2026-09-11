//! The callbacks the line editor and the pinned logo drive: the idle poll that surfaces a
//! peer's change while the user sits at the prompt, the logo's click/double-click accent
//! changes, the pending-image hooks behind Ctrl-V, and Ctrl-S's selection copy.
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

// Idle hook: while the user sits at the prompt, poll the live events feed (so a peer's
// name/colour change surfaces without a keystroke) and, in screen mode, re-measure the
// terminal so a resize repaints — no SIGWINCH handler needed.
pub const IdleCtx = struct { session: *Session, io: std.Io, screen: ?*screen.Screen = null };
pub fn idleTick(raw: *anyopaque) bool {
    const c: *IdleCtx = @ptrCast(@alignCast(raw));
    const resized = if (c.screen) |s| s.tick() else false;
    const evented = remoteEvents.pollEvents(c.session, c.io);
    return resized or evented; // true → the line editor repaints the prompt
}

// Single-click on the pinned logo: advance the accent, mirroring the browser logo.
pub fn logoCycle(raw: *anyopaque) void {
    const c: *IdleCtx = @ptrCast(@alignCast(raw));
    handlers.cycleTheme(c.session);
}

// Double-click on the pinned logo: set a random custom colour outside the preset list. The
// click time seeds the RNG so each double-click lands on a different hue.
pub fn logoCustom(raw: *anyopaque) void {
    const c: *IdleCtx = @ptrCast(@alignCast(raw));
    const seed: u64 = @bitCast(std.Io.Clock.now(.awake, c.io).toMilliseconds());
    handlers.randomCustomTheme(c.session, seed);
}

// The line editor's pending-image hooks (line_edit.PendingImages): Ctrl-V — and a ⌘V that
// pastes an image file's path — holds a picture against the line being typed, shown there as
// an `[Image #N …]` marker. Submitting the line turns them into uploads; deleting a marker,
// or abandoning the line, drops what it stood for.
pub fn pendingPaste(raw: *anyopaque, before: []const u8, label: []u8, text: []u8) line_edit.PasteResult {
    const c: *IdleCtx = @ptrCast(@alignCast(raw));
    return attachments.pasteAtPrompt(c.session, c.io, before, label, text);
}

pub fn pendingAddPath(raw: *anyopaque, text: []const u8, before: []const u8, label: []u8) ?usize {
    const c: *IdleCtx = @ptrCast(@alignCast(raw));
    return attachments.capturePendingPath(c.session, c.io, text, before, label);
}

pub fn pendingKeep(raw: *anyopaque, kept: []const usize) void {
    const c: *IdleCtx = @ptrCast(@alignCast(raw));
    c.session.keepPending(kept);
}

pub fn pendingCount(raw: *anyopaque) usize {
    const c: *IdleCtx = @ptrCast(@alignCast(raw));
    return c.session.pending.items.len;
}

// Ctrl-S in full-screen mode: copy the current visual selection to the clipboard and note it.
pub fn copySelection(raw: *anyopaque, text: []const u8) void {
    const c: *IdleCtx = @ptrCast(@alignCast(raw));
    clipboard.writeText(c.session.gpa, c.io, text) catch {
        logo.err("could not copy the selection to the clipboard\n", .{});
        return;
    };
    logo.print("copied {d} chars to the clipboard\n", .{text.len});
}

pub fn pollCancel(ctx: *anyopaque, timeout_ms: i32) bool {
    const ed: *line_edit.Editor = @ptrCast(@alignCast(ctx));
    return ed.pollInterrupt(timeout_ms);
}

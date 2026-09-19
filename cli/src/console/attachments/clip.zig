//! The clipboard commands: /copy writes the working image out, /paste reads one in, and
//! /unpaste takes the newest attachment back. An EMPTY clipboard is not a failure.
const std = @import("std");
const image = @import("../../image.zig");
const pipeline = @import("../../pipeline.zig");
const logo = @import("../../logo.zig");
const core = @import("../../core.zig");
const clipboard = @import("../../clipboard.zig");
const commands = @import("../commands.zig");
const line_edit = @import("../../line_edit.zig");
const ui = @import("../ui.zig");
const Session = @import("../session.zig").Session;
const Attachment = @import("../session.zig").Attachment;
const handlers = @import("../handlers.zig");
const llmPrompt = @import("../llmPrompt.zig");

pub fn doCopy(session: *Session, io: std.Io) !void {
    if (!session.hasImage()) return ui.noImage();
    const img = session.current().*;
    const png = image.encode(session.gpa, img, .png) catch |e| {
        logo.err("could not encode the image for the clipboard ({s})\n", .{@errorName(e)});
        return;
    };
    defer session.gpa.free(png);
    clipboard.writeImage(session.gpa, io, png) catch |e| return clipError("copy", e);
    logo.print("copied to clipboard ({d}x{d})\n", .{ img.width, img.height });
}

pub fn doPaste(session: *Session, io: std.Io) !void {
    _ = try clipboardToImage(session, io, false);
}

/// Load the clipboard's image as the working image AND a turn attachment, as an `/upload` does.
/// `quiet` swallows the "nothing to take" outcomes for a bare `/upload`; a real failure prints.
pub fn clipboardToImage(session: *Session, io: std.Io, quiet: bool) !bool {
    const bytes = clipboard.readImage(session.gpa, io) catch |e| {
        if (!quiet or !isEmptyClipboard(e)) clipError("paste", e);
        return false;
    };
    defer session.gpa.free(bytes);
    const img = image.decode(session.gpa, bytes) catch |e| {
        logo.err("the clipboard image could not be decoded ({s})\n", .{@errorName(e)});
        return false;
    };
    // §2.1: a paste joins the turn's attachments exactly like an /upload, so several pasted pictures
    // all ride the next /prompt. Best-effort: a copy we cannot afford just is not one.
    const att_bytes: ?[]u8 = session.gpa.dupe(u8, bytes) catch null;
    errdefer if (att_bytes) |b| session.gpa.free(b);
    try session.loadImage(img, "clipboard", true, .png, null);
    if (att_bytes) |b| session.addAttachment("clipboard", b, .png, true) catch {};
    ui.redraw(session);
    // Say what just landed and how to work with it — a pasted picture you cannot see listed
    // is one you cannot refer to (`image` index N) or take back.
    const n_att = session.liveAttachments().len;
    if (n_att > 1) {
        logo.note("pasted as image {d} of {d} this turn — '/images' lists them, Ctrl-Z removes it\n", .{ n_att, n_att });
    } else {
        logo.note("pasted from the clipboard — Ctrl-Z removes it\n", .{});
    }
    return true;
}

// Clipboard outcomes that mean "nothing to paste" rather than a failure — the cases a bare
// `/upload` treats as "no image was offered" and answers with its usage line.
fn isEmptyClipboard(e: anyerror) bool {
    return e == clipboard.Error.NoImage or e == clipboard.Error.Unsupported or e == clipboard.Error.ToolMissing;
}

/// `/unpaste` (Ctrl-Alt-Z) — take back the last image added this turn: the newest attachment goes
/// and the one before it becomes the working image; with none left the working image drops.
pub fn doUnpaste(session: *Session, arg: []const u8) void {
    // `/unpaste 2` takes back a SPECIFIC one (the numbering `/images` prints); bare takes the
    // newest, which is what the Ctrl-Z chord sends.
    const trimmed = std.mem.trim(u8, arg, " \t");
    if (trimmed.len != 0) {
        const n = std.fmt.parseInt(usize, trimmed, 10) catch 0;
        if (n == 0 or n > session.liveAttachments().len) {
            logo.err("no attached image {s} — '/images' lists them\n", .{trimmed});
            return;
        }
        var gone = session.removeAttachment(n - 1) orelse return;
        defer gone.deinit(session.gpa);
        logo.note("removed image {d} \"{s}\" — {d} left this turn\n", .{ n, gone.label, session.liveAttachments().len });
        return;
    }
    if (session.popAttachment()) |att| {
        var gone = att;
        defer gone.deinit(session.gpa);
        if (session.lastAttachment()) |prev| {
            // Reload the previous attachment as the working image. loadImage resets the edit
            // history (a different picture) but leaves the attachment list alone.
            const img = image.decode(session.gpa, prev.bytes) catch |e| {
                logo.err("could not restore \"{s}\" ({s})\n", .{ prev.label, @errorName(e) });
                return;
            };
            const label = prev.label;
            const source = session.gpa.dupe(u8, prev.bytes) catch null;
            session.loadImage(img, label, prev.temp, prev.fmt, source) catch |e| {
                logo.err("could not restore \"{s}\" ({s})\n", .{ label, @errorName(e) });
                return;
            };
            logo.note("removed \"{s}\" — back to \"{s}\"\n", .{ gone.label, label });
        } else {
            session.clearAll();
            logo.note("removed \"{s}\" — no image left\n", .{gone.label});
        }
        ui.redraw(session);
        return;
    }
    if (!session.hasImage()) {
        logo.note("nothing to remove — no working image and no attachments this turn\n", .{});
        return;
    }
    handlers.doDrop(session);
}

pub fn clipError(verb: []const u8, e: anyerror) void {
    switch (e) {
        clipboard.Error.Unsupported => logo.err("clipboard {s} is only supported on macOS\n", .{verb}),
        clipboard.Error.ToolMissing => logo.err("'osascript' not found — clipboard {s} needs macOS\n", .{verb}),
        clipboard.Error.NoImage => logo.err("no image on the clipboard to paste\n", .{}),
        else => logo.err("clipboard {s} failed ({s})\n", .{ verb, @errorName(e) }),
    }
}

// images pasted INTO the line being typed (Ctrl-V, or a pasted image path)

//! Clipboard and pending-attachment machinery for the console: /copy, /paste,
//! /unpaste and /images, plus the images pasted INTO the line being typed
//! (Ctrl-V or a pasted path) that become the turn's uploads on submit.
const std = @import("std");
const logo = @import("../app/logo.zig");
const Session = @import("session.zig").Session;
const clip = @import("attachments/clip.zig");
const pending = @import("attachments/pending.zig");

pub const doCopy = clip.doCopy;
pub const doPaste = clip.doPaste;
pub const clipboardToImage = clip.clipboardToImage;
pub const doUnpaste = clip.doUnpaste;

pub const pasteAtPrompt = pending.pasteAtPrompt;
pub const capturePendingPath = pending.capturePendingPath;
pub const drainPending = pending.drainPending;

/// `/images` — what the next `/prompt` will send along: every image added this turn, in the
/// order an `{"op":"image","index":N}` action indexes them, with the working one marked.
pub fn doImages(session: *Session) void {
    const items = session.liveAttachments();
    if (items.len == 0) {
        logo.print("no images attached to this turn — /upload, /paste or Ctrl-V adds one\n", .{});
        return;
    }
    logo.print("{d} image(s) on this turn — /prompt sends them all, /unpaste <n> drops one:\n", .{items.len});
    for (items, 1..) |att, i| {
        const current = session.label != null and std.mem.eql(u8, session.label.?, att.label);
        logo.print("  {d}. {s} ({d} KB){s}\n", .{
            i,
            att.label,
            (att.bytes.len + 1023) / 1024,
            if (current) " — the working image" else "",
        });
    }
}

test {
    _ = clip;
    _ = pending;
}

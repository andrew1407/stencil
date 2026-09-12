//! The console settings-op profile (the CLI's §10 analog): ops that drive the console's
//! OWN controls, the way the GUI editors' §10 block drives theirs. Every one maps 1:1 onto
//! an existing console command, and their bullets form the spliced settings block.
const std = @import("std");
const opplan = @import("../opplan.zig");
const descriptor = @import("descriptor.zig");

const Action = opplan.Action;
const OpDescriptor = descriptor.OpDescriptor;
const OpCapability = descriptor.OpCapability;

pub const ops = [_]OpDescriptor{
    .{
        .name = "accent",
        .tag = .accent,
        .capability = .theme,
        .console_bullet =
        \\- {"op":"accent","color":"#7c3aed"} — set the console's colour theme (its accent).
        \\  "color" must be a #rrggbb hex, so translate colour names yourself (cyan =
        \\  "#00ffff"). The console has no light/dark mode — a theme request means this op.
        \\- "accent" also accepts {"op":"accent","preset":"green"} — one of the console's
        \\  named /theme presets; use a preset when the user names a colour that has one.
        ,
    },
    .{
        .name = "connect",
        .tag = .connect,
        .capability = .network,
        .console_bullet =
        \\- {"op":"connect","server":"..."} / {"op":"disconnect","server":"..."} — manage the
        \\  user's collaboration-server connections. Only a server listed in the console
        \\  state below may be named — never invent, complete, or suggest a new address; for
        \\  a server not listed there, tell the user to run '/connect <url>' themselves.
        ,
    },
    // disconnect rides connect's bullet.
    .{ .name = "disconnect", .tag = .disconnect, .capability = .network },
    .{
        .name = "reconnect",
        .tag = .reconnect,
        .capability = .network,
        .console_bullet =
        \\- {"op":"reconnect","server":"..."} — re-establish a connection that went stale
        \\  (the console's /reconnect); the same server rule as connect.
        ,
    },
    .{
        .name = "delete",
        .tag = .delete,
        .capability = .filesystem,
        .console_bullet =
        \\- {"op":"delete","path":"old.stencil"} — delete a LOCAL .stencil project file in
        \\  the working directory (the console's /delete). Only .stencil files, never a URL
        \\  or a path outside the working directory.
        ,
    },
    .{
        .name = "openFile",
        .tag = .open_file,
        .capability = .filesystem,
        .console_bullet =
        \\- {"op":"openFile","path":"~/Pictures/portrait.png"} — load a LOCAL file the user named
        \\  as the working image: an image or video, a layout ".json" (drawn onto the current
        \\  picture), or a ".stencil" project. ONLY a path the user themselves wrote in this
        \\  conversation — never invent, complete, guess or list one, and never a directory.
        ,
    },
    .{
        .name = "openUrl",
        .tag = .open_url,
        .capability = .network,
        .console_bullet =
        \\- {"op":"openUrl","url":"https://…"} — load an image (or video frame) from a URL
        \\  as the working image (the console's /upload). ONLY a URL the user themselves
        \\  wrote in this conversation — never introduce, complete, or rewrite one.
        ,
    },
    .{
        .name = "copy",
        .tag = .copy,
        .capability = .clipboard,
        .console_bullet =
        \\- {"op":"copy"} — copy the current rendered image to the system clipboard. This IS
        \\  what "copy the result / copy to clipboard" means; never answer that it cannot be
        \\  done. Takes no fields.
        ,
    },
    .{
        .name = "clear",
        .tag = .clear,
        .console_bullet =
        \\- {"op":"clear"} — REMOVE the working image and its lines, leaving the editor empty.
        \\  This is what "remove/delete/clear the image" means. Never answer that with
        \\  {"op":"blank"}: a blank REPLACES the picture with a white page, which is not a
        \\  removal. Takes no fields.
        ,
    },
    .{
        .name = "clearChat",
        .tag = .clear_chat,
        .console_bullet =
        \\- {"op":"clearChat"} — clear THIS conversation's history; the app asks the user to
        \\  confirm first, and the clear happens after this plan's other actions finish. This IS
        \\  what "clear the chat / conversation / history" means; never answer that it cannot be
        \\  done. Takes no fields.
        ,
    },
};

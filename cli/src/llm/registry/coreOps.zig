//! The §2 image-edit ops, in prompt order — their bullets are §4's "Available ops" list.
const std = @import("std");
const opplan = @import("../opplan.zig");
const descriptor = @import("descriptor.zig");

const Action = opplan.Action;
const OpDescriptor = descriptor.OpDescriptor;
const OpCapability = descriptor.OpCapability;

/// One §13 descriptor per core op (crop … clearChat's siblings live in consoleOps.zig).
pub const ops = [_]OpDescriptor{
    .{
        .name = "crop",
        .tag = .crop,
        .bullet =
        \\- {"op":"crop","spec":{"x1":"10%","x2":"-10%","aspect":"3:4"}} — move edges inward;
        \\  tokens are numbers with optional unit % / px / cm / in; a leading "-" measures from the
        \\  opposite side. Include only the edges you want to move. For a target aspect ratio add
        \\  "aspect":"W:H" INSIDE "spec", never beside it (portrait "3:4", album/landscape "4:3",
        \\  square "1:1") — the editor cuts the resolved crop to that exact ratio about its centre,
        \\  so NEVER derive ratio tokens yourself; combine it with edge tokens when a specific
        \\  region should be kept.
        ,
        .console_addendum =
        \\- The crop op's "spec" also takes "album": true — derive the missing crop axis from
        \\  the page format in landscape orientation (the console's '/crop … album').
        ,
    },
    .{
        .name = "rotate",
        .tag = .rotate,
        .bullet =
        \\- {"op":"rotate","dir":"left"|"right","times":1..3} — quarter turns only.
        ,
    },
    .{
        .name = "filter",
        .tag = .filter,
        .bullet =
        \\- {"op":"filter","mode":"none"|"bw"|"sepia"|"invert"|"contour"|"custom","tint":"#rrggbb"}
        \\  — "custom" is a duotone tint and requires "tint"; "contour" is edge detection.
        ,
    },
    .{
        .name = "layout",
        .tag = .layout,
        .bullet =
        \\- {"op":"layout","lines":[{"points":[{"x":0,"y":0},...],"color":"#FFFF00","thickness":2,
        \\  "pointSize":4,"style":"solid"|"dashed"|"dotted","locked":false,"fillColor":"transparent"}]}
        \\  — draw annotation polylines in image-pixel coordinates. When asked to extract lines,
        \\  shapes, or structure from an attached image, answer with this op. An empty "lines"
        \\  array REMOVES every drawn line — that is what "clear/remove the lines" means.
        ,
    },
    .{
        .name = "formula",
        .tag = .formula,
        .bullet =
        \\- {"op":"formula","axis":"x"|"y","expr":"x*2+10"} — coordinate transform; single variable
        \\  matching the axis; operators + - * / ** and parentheses only. An empty "expr" clears
        \\  that axis; {"op":"formula","enabled":false} switches formulas OFF entirely.
        ,
    },
    .{
        .name = "page",
        .tag = .page,
        .bullet =
        \\- {"op":"page","format":"a4"} — ISO page formats a0–a10, b0–b10, c0–c10 — or a custom
        \\  size: {"op":"page","width":20,"height":30} in centimetres (one form or the other).
        ,
    },
    .{
        .name = "blank",
        .tag = .blank,
        .bullet =
        \\- {"op":"blank","color":"#ffffff","format":"a4"} — create a blank page; explicit
        \\  centimetre dims ride as "width"/"height" instead of "format".
        ,
    },
    .{
        .name = "undo",
        .tag = .undo,
        .bullet =
        \\- {"op":"undo","steps":1} / {"op":"redo","steps":1} — step this surface's edit history.
        \\  "Undo that" means {"op":"undo"}; steps count history entries, which can be finer
        \\  than one request.
        ,
    },
    // redo rides undo's bullet; reset is parsed per §2 but never advertised.
    .{ .name = "redo", .tag = .redo },
    .{ .name = "reset", .tag = .reset },
    .{
        .name = "frame",
        .tag = .frame,
        .bullet =
        \\- {"op":"frame","index":0} or {"op":"frame","indices":[0,30,60]} — pick video frame(s);
        \\  only valid when the current input is a video.
        ,
    },
    .{
        .name = "image",
        .tag = .image,
        .bullet =
        \\- {"op":"image","index":1} — switch the working image to the Nth image attached to THIS
        \\  message (1-based, in attachment order); coordinates in later actions are in THAT
        \\  image's pixel frame. Only valid when the user attached images. Use it to edit several
        \\  attached images in one plan, giving each image its OWN actions.
        ,
    },
    .{
        .name = "save",
        .tag = .save,
        .bullet =
        \\- {"op":"save","name":"portrait 1","path":"~/Downloads"} — save the current image with its
        \\  drawn lines. `path` is optional and may be a folder or a file name (".stencil" saves the
        \\  whole project, an image extension saves the picture); with no path it writes a project
        \\  into the working directory. ONLY a path the user themselves wrote in this conversation —
        \\  never invent, complete or rewrite one. When the user asks to process several images and
        \\  keep the results, finish each image's actions with a "save" before switching to the
        \\  next: image 1, its edits, save, image 2, its edits, save, …
        ,
    },
};

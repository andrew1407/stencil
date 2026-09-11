//! Pasted pictures ride the line as one `[image N]` marker each: finding a marker, deleting it
//! whole, and stripping the markers off the text that is finally submitted.
const PasteResult = @import("../line_edit.zig").PasteResult;
const std = @import("std");

/// The widest name a marker shows — the host elides longer ones into this.
pub const max_marker_label = 20;
// What a pasted image leaves in the line: "[Image #N <label>]", the index at a fixed offset
// so dropping one renumbers the rest with a single byte write.
pub const marker_open = "[Image #";

/// How many images one prompt line may carry (the cap Claude Code's CLI uses; a turn can
/// still hold more by /upload-ing as well).
pub const max_pending_images = 3;

pub const PendingImages = struct {
    ctx: *anyopaque,
    /// What Ctrl-V does with the clipboard, for the line `before` — which also says how many
    /// images that line may carry (`/upload` loads one picture, a `/prompt` up to three; the
    /// host knows the verbs). A picture is held and its marker label written into `label`; with
    /// no picture the clipboard's TEXT is written into `text` and simply typed into the line.
    /// `.none` means there was nothing to take (the hook said why).
    paste: *const fn (ctx: *anyopaque, before: []const u8, label: []u8, text: []u8) PasteResult,
    /// Consider a bracketed paste — `text`, typed after `before` — as an image file's path;
    /// same contract, and silent when it declines (the paste is then ordinary text).
    addPath: *const fn (ctx: *anyopaque, text: []const u8, before: []const u8, label: []u8) ?usize,
    /// Keep only these images (0-based, in the order the line's markers now read).
    keep: *const fn (ctx: *anyopaque, kept: []const usize) void,
    count: *const fn (ctx: *anyopaque) usize,
};

/// The end offset (exclusive) of the `[Image #N …]` marker starting at `i`, or null when no
/// marker starts there. The index is one digit, so a marker is always removed and renumbered
/// as a whole.
pub fn markerEnd(line: []const u8, i: usize) ?usize {
    if (i + marker_open.len + 2 > line.len) return null;
    if (!std.mem.eql(u8, line[i..][0..marker_open.len], marker_open)) return null;
    const d = line[i + marker_open.len];
    if (d < '1' or d > '0' + max_pending_images) return null;
    const rest = line[i + marker_open.len + 1 ..];
    const close = std.mem.indexOfScalar(u8, rest, ']') orelse return null;
    return i + marker_open.len + 1 + close + 1;
}

/// The start of the marker ENDING at `pos`, or null when the cursor isn't right behind one.
pub fn markerBefore(line: []const u8, pos: usize) ?usize {
    if (pos == 0 or pos > line.len or line[pos - 1] != ']') return null;
    var i = pos - 1;
    while (true) : (i -= 1) {
        if (line[i] == '[') {
            if (markerEnd(line, i)) |e| {
                if (e == pos) return i;
            }
            return null;
        }
        if (i == 0) return null;
    }
}

/// `line` with every image marker taken out (and the gap it left closed), written into `out`
/// — the command the console actually dispatches once the pictures have been lifted off it.
/// Returns `line` itself, untouched, when it carries no markers.
pub fn stripMarkers(out: []u8, line: []const u8) []const u8 {
    if (std.mem.indexOf(u8, line, marker_open) == null) return line;
    var n: usize = 0;
    var i: usize = 0;
    while (i < line.len) {
        if (markerEnd(line, i)) |e| {
            i = e;
            continue;
        }
        // Never leave a doubled (or leading) space where a marker stood.
        if (!(line[i] == ' ' and (n == 0 or out[n - 1] == ' '))) {
            out[n] = line[i];
            n += 1;
        }
        i += 1;
    }
    return std.mem.trimEnd(u8, out[0..n], " ");
}

/// Pasted text as ONE editable line: control bytes (newlines above all) become spaces, so it
/// lands the way a bracketed paste does and nothing runs until Enter.
pub fn sanitizeInline(text: []u8) []const u8 {
    for (text) |*c| {
        if (c.* < 0x20 or c.* == 0x7f) c.* = ' ';
    }
    return std.mem.trimEnd(u8, text, " ");
}

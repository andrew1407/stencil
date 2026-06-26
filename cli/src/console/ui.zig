//! Console presentation: the logo header line, per-edit acknowledgements, prompt string,
//! help/intro text and the theme listing. Holds the two bits of UI mode state (whether
//! we're driving a TTY, and the current accent key) plus the Tab-completion word list.
//! All human output goes to stderr via logo.print, keeping a piped `/save` stdout clean.
const std = @import("std");
const logo = @import("../logo.zig");
const theme = @import("../theme.zig");
const Session = @import("session.zig").Session;

var interactive: bool = false; // true when driving a TTY (enables screen clears + colour)
var accent_store: [24]u8 = undefined; // stable backing for the current accent label (incl. custom #hex)
var current_accent: []const u8 = theme.default_key;

// Command words offered by Tab-completion in the interactive editor (canonical names +
// transform shorthands), roughly in the order they appear in `help`.
pub const completions = [_][]const u8{
    "upload", "paste",  "blank", "apply", "crop", "rotate", "filter", "bw",
    "sepia",  "tint",   "none",  "exec",  "undo", "redo",   "reset",  "save",
    "copy",   "status", "theme", "clear", "drop", "help",   "exit",
};

pub fn setInteractive(v: bool) void {
    interactive = v;
}

/// Adopt a new accent label (a preset key or a custom '#hex'); the logo's RGB is set
/// separately by the caller. Copied into a stable buffer so a transient '#hex' slice is safe.
pub fn setAccent(key: []const u8) void {
    const n = @min(key.len, accent_store.len);
    @memcpy(accent_store[0..n], key[0..n]);
    current_accent = accent_store[0..n];
}

pub fn promptStr(session: *Session) []const u8 {
    return if (session.hasImage()) "stencil*> " else "stencil> "; // '*' marks an image is loaded
}

// The header line under the logo: the working image's identity + current size + edit
// position. Reprinted on /clear, /theme and after a source change (so it stays "on top").
pub fn status(session: *Session) void {
    if (!session.hasImage()) {
        logo.print("image: (none) — upload, paste, or create one first\n", .{});
        return;
    }
    const img = session.current();
    const tag = if (session.temp) "  [in-memory, temporary]" else "";
    const n = session.states.items.len;
    if (n > 1) {
        logo.print("image: {s} ({d}x{d}){s}  [{d}/{d}]\n", .{ session.label.?, img.width, img.height, tag, session.cursor + 1, n });
    } else {
        logo.print("image: {s} ({d}x{d}){s}\n", .{ session.label.?, img.width, img.height, tag });
    }
}

// A concise per-action acknowledgement (not the full identity header): "<verb> -> WxH [n/m]".
pub fn ack(session: *Session, verb: []const u8) void {
    const img = session.current();
    const n = session.states.items.len;
    if (n > 1) {
        logo.print("{s} -> {d}x{d}  [{d}/{d}]\n", .{ verb, img.width, img.height, session.cursor + 1, n });
    } else {
        logo.print("{s} -> {d}x{d}\n", .{ verb, img.width, img.height });
    }
}

// Clear the screen (interactive only) and reprint the logo + header — the "image on top".
pub fn redraw(session: *Session) void {
    if (interactive) {
        logo.print("\x1b[2J\x1b[3J\x1b[H", .{});
        logo.banner();
    }
    status(session);
}

pub fn noImage() void {
    logo.print("error: no image loaded — use '/upload <path|url>', '/paste', or '/blank ...' first\n", .{});
}

pub fn listThemes() void {
    logo.print("Themes (current: {s}) — '/theme <name | #hex | default>' to switch:\n", .{current_accent});
    const on = logo.colorEnabled();
    for (theme.accents) |a| {
        const mark: []const u8 = if (std.ascii.eqlIgnoreCase(a.key, current_accent)) "*" else " ";
        const tag: []const u8 = if (std.ascii.eqlIgnoreCase(a.key, theme.default_key)) " (default)" else "";
        if (on) {
            var fbuf: [20]u8 = undefined;
            const seq = std.fmt.bufPrint(&fbuf, "\x1b[38;2;{d};{d};{d}m", .{ a.rgb[0], a.rgb[1], a.rgb[2] }) catch "";
            logo.print(" {s} {s}{s}{s}{s}\n", .{ mark, seq, a.key, logo.resetSeq(), tag });
        } else {
            logo.print(" {s} {s}{s}\n", .{ mark, a.key, tag });
        }
    }
}

pub fn intro() void {
    logo.print(
        \\Console mode — '/command <args>' (the '/' is optional). Tab completes, Up/Down
        \\recall history; Ctrl-V pastes / Ctrl-C copies an image. '/help' lists commands,
        \\'/theme' changes colour, '/exit' (or Ctrl-C twice) leaves.
        \\
    , .{});
}

pub fn help() void {
    logo.print(
        \\Commands  (a leading '/' is optional)
        \\  /upload <path|url>      load an image or video frame as the working image
        \\  /paste                  load an image from the clipboard (macOS)
        \\  /blank [w h] [color]    create a blank page (default A4 @ 96dpi, white)
        \\  /apply <file.json>      draw a layout JSON onto the image
        \\  /crop <spec> [album]    crop, e.g. "x1=10% x2=90% y1=10% y2=90%"
        \\  /rotate <int>           rotate int*90 degrees (e.g. -1, 2, 3)
        \\  /filter <mode>          bw | sepia | none | a colour name/#hex (duotone tint)
        \\  /exec <action> ...      run a transform by name (crop | rotate | filter | apply)
        \\  /undo   /redo           step back / forward through edits
        \\  /reset                  revert to the original, dropping all edits
        \\  /save [path]            write to a file; a bare /save pushes to the active server project
        \\  /connect <url[ url2]>   connect to one or more collaboration servers
        \\  /connections            list connected servers
        \\  /disconnect [url]       close a connection (or the most recent)
        \\  /fetch <name> [url]     load a server project's image to keep editing
        \\  /sync <on|off>          auto-upload the result to the active project on edit/save
        \\  /copy                   copy the current image to the clipboard (macOS)
        \\  /status                 show the working image (path, size, edit position)
        \\  /theme [name]           list or switch the accent colour (default violet)
        \\  /clear                  clear the screen, redraw the logo + image header
        \\  /drop                   forget the working image entirely
        \\  /help   /exit           show this list / leave (Ctrl-D, or Ctrl-C twice)
        \\Shortcuts  Ctrl-V paste an image · Ctrl-C copy the image (twice to exit)
        \\
    , .{});
}

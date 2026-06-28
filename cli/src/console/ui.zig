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
    "upload",      "paste",    "blank",       "apply",    "crop",   "rotate",
    "filter",      "bw",       "sepia",       "tint",     "none",   "exec",
    "undo",        "redo",     "reset",       "save",     "connect", "connections",
    "disconnect",  "reconnect", "projects",   "fetch",    "sync",   "copy",
    "status",      "theme",    "clear",       "drop",     "help",   "exit",
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
    const n = session.stateCount();
    if (n > 1) {
        logo.print("image: {s} ({d}x{d}){s}  [{d}/{d}]\n", .{ session.label.?, img.width, img.height, tag, session.cursor + 1, n });
    } else {
        logo.print("image: {s} ({d}x{d}){s}\n", .{ session.label.?, img.width, img.height, tag });
    }
}

// A concise per-action acknowledgement (not the full identity header): "<verb> -> WxH [n/m]".
pub fn ack(session: *Session, verb: []const u8) void {
    const img = session.current();
    const n = session.stateCount();
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
        \\recall history; Ctrl-Alt-V pastes / Ctrl-Alt-C copies an image. '/help' lists
        \\commands, '/theme' changes colour, '/exit' (or Ctrl-C twice) leaves.
        \\
    , .{});
}

// Help is printed with the section headers + command names in the current accent colour
// (accentSeq()/resetSeq() are "" when colour is off, so piped output stays plain).
const help_spaces = " " ** 32;

fn helpSection(accent: []const u8, reset: []const u8, title: []const u8) void {
    logo.print("\n{s}{s}{s}\n", .{ accent, title, reset });
}

fn helpRow(accent: []const u8, reset: []const u8, cmd: []const u8, desc: []const u8) void {
    const width = 24; // command column; descriptions line up after it
    const pad = if (cmd.len < width) help_spaces[0 .. width - cmd.len] else help_spaces[0..1];
    logo.print("  {s}{s}{s}{s}{s}\n", .{ accent, cmd, reset, pad, desc });
}

pub fn help() void {
    const a = logo.accentSeq();
    const r = logo.resetSeq();
    logo.print("Commands  (a leading '/' is optional)\n", .{});

    helpSection(a, r, "Image");
    helpRow(a, r, "/upload <path|url>", "load an image or video frame as the working image");
    helpRow(a, r, "/paste", "load an image from the clipboard (macOS)");
    helpRow(a, r, "/blank [w h] [color]", "create a blank page (default A4 @ 96dpi, white)");

    helpSection(a, r, "Edit");
    helpRow(a, r, "/apply <file.json>", "draw a layout JSON onto the image");
    helpRow(a, r, "/crop <spec> [album]", "crop, e.g. \"x1=10% x2=90% y1=10% y2=90%\"");
    helpRow(a, r, "/rotate <int>", "rotate int*90 degrees (e.g. -1, 2, 3)");
    helpRow(a, r, "/filter <mode>", "bw | sepia | none | a colour name/#hex (duotone tint)");
    helpRow(a, r, "/exec <action> ...", "run a transform by name (crop | rotate | filter | apply)");
    helpRow(a, r, "/undo   /redo", "step back / forward through edits");
    helpRow(a, r, "/reset", "revert to the original, dropping all edits");

    helpSection(a, r, "Save");
    helpRow(a, r, "/save [path]", "write to a file; a bare /save pushes to the active server project");
    helpRow(a, r, "/copy", "copy the current image to the clipboard (macOS)");

    helpSection(a, r, "Connections");
    helpRow(a, r, "/connect <url[ url2]>", "connect to one or more collaboration servers");
    helpRow(a, r, "/connections", "list connected servers + reachability status");
    helpRow(a, r, "/disconnect [url]", "close a connection (or the most recent)");
    helpRow(a, r, "/reconnect [url]", "re-establish one connection (or all) and the live feed");
    helpRow(a, r, "/projects [url]", "list projects on a server (or all connected servers)");
    helpRow(a, r, "/fetch <name> [url]", "load a server project's image to keep editing");
    helpRow(a, r, "/sync [on|off]", "live mode (bare /sync toggles): push edits + pull peers' changes");

    helpSection(a, r, "System");
    helpRow(a, r, "/status", "show the working image (path, size, edit position)");
    helpRow(a, r, "/theme [name]", "list or switch the accent colour (default violet)");
    helpRow(a, r, "/clear", "clear the screen, redraw the logo + image header");
    helpRow(a, r, "/drop", "forget the working image entirely");
    helpRow(a, r, "/help   /exit", "show this list / leave (Ctrl-D, or Ctrl-C twice)");

    logo.print("\nShortcuts  Ctrl-Alt-V paste · Ctrl-Alt-C copy an image · Ctrl-C twice to exit\n", .{});
}

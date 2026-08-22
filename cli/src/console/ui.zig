//! Console presentation: the logo header line, per-edit acknowledgements, prompt string,
//! help/intro text and the theme listing. Holds the two bits of UI mode state (whether
//! we're driving a TTY, and the current accent key) plus the Tab-completion word list.
//! All human output goes to stderr via logo.print, keeping a piped `/save` stdout clean.
const std = @import("std");
const logo = @import("../logo.zig");
const theme = @import("../theme.zig");
const core = @import("../core.zig");
const screen = @import("screen.zig");
const commands = @import("commands.zig");
const Session = @import("session.zig").Session;

var interactive: bool = false; // true when driving a TTY (enables screen clears + colour)
var accent_store: [24]u8 = undefined; // stable backing for the current accent label (incl. custom #hex)
var current_accent: []const u8 = theme.default_key;

// Command words offered by Tab-completion in the interactive editor (canonical names +
// transform shorthands), roughly in the order they appear in `help`.
pub const completions = [_][]const u8{
    "upload",        "source-upload", "scrape",              "paste",       "unpaste",         "images",       "blank",        "apply",
    "crop",          "rotate",        "filter",              "bw",          "sepia",           "invert",       "contour",      "tint",
    "none",          "exec",          "undo",                "redo",        "reset",           "save",         "delete",       "layout",
    "formula",       "format",        "connect",             "connections", "disconnect",      "reconnect",    "projects",     "rename",
    "project-color", "blank-color",   "project-description", "keywords",    "keywords-search", "keywords-add", "keywords-del", "expire",
    "fetch",         "sync",          "copy",                "status",      "theme",           "mouse",        "reveal-speed", "clear",
    "drop",          "prompt",        "llm",                 "chat",        "help",            "exit",
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

/// The active accent label (a preset key or a custom '#hex') — used by the logo single-click
/// to compute the next accent.
pub fn currentAccentKey() []const u8 {
    return current_accent;
}

pub fn promptStr(session: *Session) []const u8 {
    // The prompt is the caret, nothing else: a bare '>' in full-screen mode (the pinned header
    // already names the app), 'stencil>' in the plain one. It renders in the accent — that IS
    // the theme showing where input goes — and carries no '*' image marker, which duplicated
    // what the image header line says one row up.
    _ = session;
    return if (screen.current() != null) "> " else "stencil> ";
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
    // Page format shown next to the px size (best-effort).
    const fmt = session.pageFormatLabel() catch null;
    defer if (fmt) |f| session.gpa.free(f);
    const fmt_s = if (fmt) |f| f else "";
    // Paint a fetched project's name in its custom colour (or the neutral default), so the header
    // mirrors the /projects table and the GUI front-ends. Plain for local/temp images.
    var cbuf: [20]u8 = undefined;
    const seq = nameColorSeq(session, &cbuf);
    const rst = if (seq.len != 0) logo.resetSeq() else "";
    if (n > 1) {
        logo.print("image: {s}{s}{s} ({d}x{d} px · {s}){s}  [{d}/{d}]\n", .{ seq, session.label.?, rst, img.width, img.height, fmt_s, tag, session.cursor + 1, n });
    } else {
        logo.print("image: {s}{s}{s} ({d}x{d} px · {s}){s}\n", .{ seq, session.label.?, rst, img.width, img.height, fmt_s, tag });
    }
}

/// The SGR escape painting the active project's name in its custom colour (or the neutral default)
/// — but only for a fetched server project. "" otherwise (local/temp images), so the name prints
/// plain. theme.nameSeq applies the no-colour-mode gate.
fn nameColorSeq(session: *Session, buf: []u8) []const u8 {
    if (!session.hasRemote()) return "";
    return theme.nameSeq(session.remote_color orelse "", buf);
}

// A concise per-action acknowledgement (not the full identity header): "<verb> -> WxH · fmt [n/m]".
pub fn ack(session: *Session, verb: []const u8) void {
    const img = session.current();
    const n = session.stateCount();
    const fmt = session.pageFormatLabel() catch null;
    defer if (fmt) |f| session.gpa.free(f);
    const fmt_s = if (fmt) |f| f else "";
    if (n > 1) {
        logo.print("{s} -> {d}x{d} px · {s}  [{d}/{d}]\n", .{ verb, img.width, img.height, fmt_s, session.cursor + 1, n });
    } else {
        logo.print("{s} -> {d}x{d} px · {s}\n", .{ verb, img.width, img.height, fmt_s });
    }
}

// Clear the screen (interactive only) and reprint the logo + header — the "image on top".
// In full-screen mode the logo is a pinned header, so just clear the scrollback and reprint
// the status line into it.
pub fn redraw(session: *Session) void {
    if (screen.current()) |s| {
        s.clearScrollback();
        status(session);
        return;
    }
    if (interactive) {
        logo.print("\x1b[2J\x1b[3J\x1b[H", .{});
        logo.banner();
    }
    status(session);
}

pub fn noImage() void {
    logo.err("no image loaded — use '/upload <path|url>', '/paste', or '/blank ...' first\n", .{});
}

pub fn listThemes() void {
    logo.print("Themes (current: {s}) — '/theme <name | #hex | default>' to switch:\n", .{current_accent});
    const on = logo.colorEnabled();
    for (theme.accents()) |a| {
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

/// `/filter` with no argument: list the accepted modes (short list, not one error line).
pub fn listFilters() void {
    logo.print("Filters — '/filter <mode>' to apply:\n", .{});
    logo.print("   bw        greyscale\n", .{});
    logo.print("   sepia     warm brown tone\n", .{});
    logo.print("   invert    negative (flip every channel)\n", .{});
    logo.print("   contour   edge detection (dark edges on white)\n", .{});
    logo.print("   none      remove the filter\n", .{});
    logo.print("   <colour>  a name or #hex makes a duotone tint (e.g. '/filter teal')\n", .{});
}

/// `/format` with no argument: list every named page format with its cm size, marking the
/// session's current pick (the same pattern as listThemes), plus the custom-variant hint.
pub fn listFormats(session: *Session) void {
    // The effective pick: an explicit format, else the A4 default the session falls back to.
    const current: []const u8 = if (session.page_size.len != 0) session.page_size else "A4";
    logo.print("Page formats (current: {s}) — '/format <name>' to switch:\n", .{current});
    var it = std.mem.tokenizeScalar(u8, core.pageFormats(), ' ');
    while (it.next()) |name| {
        const mark: []const u8 = if (std.ascii.eqlIgnoreCase(name, current)) "*" else " ";
        const tag: []const u8 = if (std.mem.eql(u8, name, "A4")) " (default)" else "";
        const p = core.namedPageSize(session.gpa, name) orelse continue;
        logo.print(" {s} {s:<4} {d:>5} × {d:>5} cm{s}\n", .{ mark, name, p.w, p.h, tag });
    }
    const cmark: []const u8 = if (std.ascii.eqlIgnoreCase("custom", current)) "*" else " ";
    logo.print(" {s} custom — set with '/format custom <w> <h>' (cm)\n", .{cmark});
}

pub fn intro() void {
    if (screen.current()) |s| {
        logo.print(
            \\Console mode — '/command <args>' (the '/' is optional). Tab completes, Up/Down
            \\recall history; Ctrl-V pastes the clipboard (an image attaches to the line, text is typed),
            \\Ctrl-C copies the selection. '/help' lists commands, '/exit' leaves.
            \\
        , .{});
        // Which selection is live, and how to reach the other one. Under mouse tracking the
        // terminal still selects with a modifier held, which is easy to not know about.
        if (s.mouseOn()) {
            logo.note("drag to select (Ctrl-S copies) — for the TERMINAL's own selection hold Shift while dragging (macOS VS Code: Option, with terminal.integrated.macOptionClickForcesSelection on), or '/mouse off'\n", .{});
        } else {
            logo.note("the terminal owns the mouse — its own selection and copy work; '/mouse on' switches to the in-app accent one (Ctrl-S copies)\n", .{});
        }
        return;
    }
    logo.print(
        \\Console mode — '/command <args>' (the '/' is optional). Tab completes, Up/Down
        \\recall history; Ctrl-V pastes the clipboard — an image attaches to the line you are typing
        \\(Backspace over its marker takes it back), text is simply typed. '/help' lists
        \\commands, '/theme' changes colour, '/mouse' switches between the terminal's own text
        \\selection and the in-app one, '/exit' (or Ctrl-C twice) leaves.
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
    helpRow(a, r, "/upload <path|url>", "load an image or video frame (bare: the clipboard's picture)");
    helpRow(a, r, "/source-upload <url> [i] [fmt] [name=]", "scrape a page and load its i-th image (alias /scrape)");
    helpRow(a, r, "/paste", "load an image from the clipboard (Ctrl-V pastes onto the line instead)");
    helpRow(a, r, "/unpaste [n]", "take back an image added this turn (bare = the last; Ctrl-Z)");
    helpRow(a, r, "/images", "list the images this turn will send to the assistant");
    helpRow(a, r, "/blank [fmt] [w h] [color]", "create a blank page (default: the picked format or A4, white)");
    helpRow(a, r, "/format [name|custom w h]", "list the page formats or pick one (drives /blank + the layout)");

    helpSection(a, r, "Edit");
    helpRow(a, r, "/apply <file.json>", "draw a layout JSON onto the image");
    helpRow(a, r, "/crop <spec> [album]", "crop, e.g. \"x1=10% x2=90% y1=10% y2=90%\"");
    helpRow(a, r, "/rotate <int>", "rotate int*90 degrees (e.g. -1, 2, 3)");
    helpRow(a, r, "/filter <mode>", "bw | sepia | invert | contour | none | a colour name/#hex (tint)");
    helpRow(a, r, "/exec <action> ...", "run a transform by name (crop | rotate | filter | apply)");
    helpRow(a, r, "/undo   /redo", "step back / forward through edits");
    helpRow(a, r, "/reset", "revert to the original, dropping all edits");

    helpSection(a, r, "Save");
    helpRow(a, r, "/save [path]", "write to a file; a bare /save pushes to the active server project");
    helpRow(a, r, "/delete <file.stencil>", "delete a local .stencil project file from disk");
    helpRow(a, r, "/layout [path]", "save the layout JSON (bare = <project>.json; a dir saves <project>.json there)");
    helpRow(a, r, "/formula [x|y <expr>|on|off|clear]", "set the x/y coord-transform formulas (saved in the layout)");
    helpRow(a, r, "/copy", "copy the current image to the clipboard");

    helpSection(a, r, "Connections");
    helpRow(a, r, "/connect <url [token]>", "connect to collaboration servers (token: session or admin, for gated servers)");
    helpRow(a, r, "/connections", "list connected servers + reachability status");
    helpRow(a, r, "/disconnect [url]", "close a connection (or the most recent)");
    helpRow(a, r, "/reconnect [url]", "re-establish one connection (or all) and the live feed");
    helpRow(a, r, "/projects [url]", "list projects on a server (or all connected servers)");
    helpRow(a, r, "/project-color [#hex]", "show or set the active project's name colour (clear = neutral grey)");
    helpRow(a, r, "/blank-color [#hex]", "show or recolour a BLANK project's background fill (blanks only)");
    helpRow(a, r, "/rename <name>", "rename the active server project (pushed live to peers)");
    helpRow(a, r, "/keywords <project>", "show a server project's search keywords (by name)");
    helpRow(a, r, "/keywords-search <kw...>", "list projects across servers matching any keyword");
    helpRow(a, r, "/keywords-add <project|[..]> <kw...>", "add keywords to one or more projects");
    helpRow(a, r, "/keywords-del <project|[..]> <kw...>", "remove keywords from one or more projects");
    helpRow(a, r, "/expire [<duration>]", "set when the active project expires (bare = formats; e.g. 'months 3', 'off')");
    helpRow(a, r, "/fetch <name> [url]", "load a server project's image to keep editing");
    helpRow(a, r, "/sync [on|off]", "live mode (bare /sync toggles): push edits + pull peers' changes");

    helpSection(a, r, "Assistant");
    helpRow(a, r, "/prompt <text>", "ask the LLM assistant; it plans + runs edits and renders variant-<label>.png files (alias /p)");
    helpRow(a, r, "/llm [key <value>]", "show or set the LLM provider config (provider | url | model | key | server; env STENCIL_LLM_*)");
    helpRow(a, r, "/chat [on|off|clear]", "opt-in: save the /prompt conversation with the project + replay it (bare /chat shows; default off)");

    helpSection(a, r, "System");
    helpRow(a, r, "/status", "show the working image (path, size, edit position)");
    helpRow(a, r, "/theme [name]", "list or switch the accent colour (default violet)");
    helpRow(a, r, "/mouse [on|off]", "full-screen: toggle mouse (off frees text selection)");
    helpRow(a, r, "/reveal-speed [0.01-1]", "full-screen: how fast output is revealed (1 = instantly, 0.5 default)");
    helpRow(a, r, "/clear", "clear the screen, redraw the logo + image header");
    helpRow(a, r, "/drop", "forget the working image entirely");
    helpRow(a, r, "/help   /exit", "show this list / leave (Ctrl-D, or Ctrl-C twice)");

    logo.print("\nShortcuts  Ctrl-V paste the clipboard — an image attaches to the line (3 on a /prompt,\n", .{});
    logo.print("           1 on an /upload; Backspace removes it), text is typed straight in\n", .{});
    logo.print("           Ctrl-C copy the selection (nothing selected: press twice to exit) · Ctrl-S also copies\n", .{});
    logo.print("           Ctrl-Z un-attach/un-paste · Ctrl-Alt-C copy the image to the clipboard\n", .{});
    logo.print("           Ctrl/Alt-Backspace delete the word back · Alt-d or Ctrl/Alt-Delete the word ahead\n", .{});
}

const testing = std.testing;

test "completions: every console command is offered by Tab-complete" {
    // Everything offered must really BE a command — a session verb or an image transform —
    // so a typo in the list can't quietly complete to nothing.
    for (completions) |w| {
        if (commands.verbOf(w) != null) continue;
        if (commands.actionOf(w, "") != null) continue;
        std.debug.print("completion '{s}' is not a command\n", .{w});
        return error.UnknownCompletion;
    }
    // …and every verb must be reachable from the list, which is what keeps a newly added
    // command from shipping without Tab-completion knowing about it (this is how /reveal,
    // /chat and /project-description were found missing).
    var missing = false;
    inline for (@typeInfo(commands.Verb).@"enum".fields) |f| {
        const want: commands.Verb = @enumFromInt(f.value);
        var found = false;
        for (completions) |w| {
            if (commands.verbOf(w)) |v| {
                if (v == want) {
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            std.debug.print("no completion offers the '{s}' command\n", .{f.name});
            missing = true;
        }
    }
    if (missing) return error.MissingCompletion;
}

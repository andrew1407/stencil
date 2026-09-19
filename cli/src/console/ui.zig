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
    "drop",          "prompt",        "llm",                 "chat",        "help",            "exit",         "script",       "script-run",
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
    // The caret alone, in the accent: '>' in full-screen mode (the pinned header already
    // names the app), 'stencil>' in the plain one. No '*' marker — the header says that.
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
    var pos: [24]u8 = undefined;
    const at = if (n > 1) std.fmt.bufPrint(&pos, "  [{d}/{d}]", .{ session.cursor + 1, n }) catch "" else "";
    logo.print("image: {s}{s}{s} ({d}x{d} px · {s}){s}{s}\n", .{ seq, session.label.?, rst, img.width, img.height, fmt_s, tag, at });
}

/// The SGR escape painting a FETCHED project's name in its custom colour (or neutral grey);
/// "" for local/temp images, so they print plain. theme.nameSeq gates no-colour mode.
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
    var pos: [24]u8 = undefined;
    const at = if (n > 1) std.fmt.bufPrint(&pos, "  [{d}/{d}]", .{ session.cursor + 1, n }) catch "" else "";
    logo.print("{s} -> {d}x{d} px · {s}{s}\n", .{ verb, img.width, img.height, fmt_s, at });
}

// Clear the screen (interactive only) and reprint the logo + header — the "image on top".
// Full-screen pins the logo already, so there it just clears the scrollback.
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
        if (!on) {
            logo.print(" {s} {s}{s}\n", .{ mark, a.key, tag });
            continue;
        }
        var fbuf: [20]u8 = undefined;
        const seq = std.fmt.bufPrint(&fbuf, "\x1b[38;2;{d};{d};{d}m", .{ a.rgb[0], a.rgb[1], a.rgb[2] }) catch "";
        logo.print(" {s} {s}{s}{s}{s}\n", .{ mark, seq, a.key, logo.resetSeq(), tag });
    }
}

/// `/filter` with no argument: list the accepted modes (short list, not one error line).
pub fn listFilters() void {
    emit(block("filters"));
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
        const p = core.namedPageSize(core.zstr(name) orelse continue) orelse continue;
        logo.print(" {s} {s:<4} {d:>5} × {d:>5} cm{s}\n", .{ mark, name, p.w, p.h, tag });
    }
    const cmark: []const u8 = if (std.ascii.eqlIgnoreCase("custom", current)) "*" else " ";
    logo.print(" {s} custom — set with '/format custom <w> <h>' (cm)\n", .{cmark});
}

pub fn intro() void {
    if (screen.current()) |s| {
        emit(block("intro-fullscreen"));
        // Which selection is live, and how to reach the other one (a held modifier).
        logo.note("{s}", .{if (s.mouseOn()) block("mouse-app") else block("mouse-terminal")});
        return;
    }
    emit(block("intro"));
}

// The console's prose — intro, filter list, command list — lives in the embedded uiText.txt;
// tests/repl_text_test.zig checks the command list against commands.zig.
const ui_text = @embedFile("uiText.txt");
const help_spaces = " " ** 32;

/// The lines of the '@name' block in uiText.txt, without its trailing newline. Comptime so a
/// missing block is a compile error, not a blank screen.
fn block(comptime name: []const u8) []const u8 {
    @setEvalBranchQuota(ui_text.len * 4);
    const at = std.mem.indexOf(u8, ui_text, "\n@" ++ name ++ "\n").? + name.len + 3;
    const rest = ui_text[at..];
    const end = std.mem.indexOf(u8, rest, "\n@") orelse return rest[0 .. rest.len - 1];
    return rest[0..end];
}

/// Print a block line by line: '§' heads a section, '¶' colours a line's first word, a TAB
/// splits a command from its description (24-column gutter), anything else is literal.
fn emit(text: []const u8) void {
    const a = logo.accentSeq();
    const r = logo.resetSeq();
    var it = std.mem.splitScalar(u8, text, '\n');
    while (it.next()) |line| {
        if (std.mem.startsWith(u8, line, "\u{a7}")) {
            logo.print("{s}{s}{s}\n", .{ a, line[2..], r });
        } else if (std.mem.startsWith(u8, line, "\u{b6}")) {
            const cut = 2 + (std.mem.indexOfScalar(u8, line[2..], ' ') orelse line.len - 2);
            logo.print("{s}{s}{s}{s}\n", .{ a, line[2..cut], r, line[cut..] });
        } else if (std.mem.indexOfScalar(u8, line, '\t')) |tab| {
            const cmd = line[0..tab];
            const width = 24; // command column; descriptions line up after it
            const pad = if (cmd.len < width) help_spaces[0 .. width - cmd.len] else help_spaces[0..1];
            logo.print("  {s}{s}{s}{s}{s}\n", .{ a, cmd, r, pad, line[tab + 1 ..] });
        } else {
            logo.print("{s}\n", .{line});
        }
    }
}

pub fn help() void {
    emit(block("help"));
}

const testing = std.testing;

test "completions: every console command is offered by Tab-complete" {
    // Everything offered must really BE a command, so a typo can't complete to nothing.
    for (completions) |w| {
        if (commands.verbOf(w) != null) continue;
        if (commands.actionOf(w, "") != null) continue;
        std.debug.print("completion '{s}' is not a command\n", .{w});
        return error.UnknownCompletion;
    }
    // …and every verb must be reachable from the list, so a new command can't ship without
    // Tab-completion (this is how /reveal, /chat and /project-description were found missing).
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

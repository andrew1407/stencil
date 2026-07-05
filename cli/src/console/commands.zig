//! Console command grammar: pure, unit-tested parsing that turns a raw input line into a
//! session `Verb` or an image-transform `Action`. No I/O and no session state — the loop in
//! console.zig dispatches on what these return. Also holds the `/blank` and crop-`album`
//! argument parsers, which are pure string work too.
const std = @import("std");
const args = @import("../args.zig");
const core = @import("../core.zig");

pub const Command = struct {
    word: []const u8, // the verb, sans any leading '/'
    arg: []const u8, // the remainder; slices into the input line; empty when none
};

// Split a line into "verb" and "arg" at the first whitespace, dropping one optional leading
// '/' (so `/upload x` and `upload x` are equivalent). A ':' or '://' is just part of the arg.
pub fn parseCommand(line: []const u8) Command {
    var s = std.mem.trim(u8, line, " \t\r\n");
    if (s.len != 0 and s[0] == '/') s = std.mem.trimStart(u8, s[1..], " \t");
    if (s.len == 0) return .{ .word = "", .arg = "" };
    if (std.mem.indexOfAny(u8, s, " \t")) |sp| {
        return .{ .word = s[0..sp], .arg = std.mem.trim(u8, s[sp + 1 ..], " \t") };
    }
    return .{ .word = s, .arg = "" };
}

pub const Verb = enum { upload, blank, save, layout, formula, format, exec, undo, redo, reset, drop, clear, copy, paste, theme, status, help, quit, connect, disconnect, reconnect, connections, projects, project_color, rename, expire, fetch, sync };

// Session-level verbs (everything that is not an image transform). Returns null for words
// that name a transform (crop/rotate/filter/apply) or are unknown.
pub fn verbOf(w: []const u8) ?Verb {
    const eq = eqIgnoreCase;
    if (eq(w, "upload") or eq(w, "open") or eq(w, "load")) return .upload;
    if (eq(w, "blank") or eq(w, "new")) return .blank;
    if (eq(w, "save") or eq(w, "write")) return .save;
    if (eq(w, "layout") or eq(w, "exportlayout") or eq(w, "savelayout")) return .layout;
    if (eq(w, "formula") or eq(w, "formulas")) return .formula;
    if (eq(w, "format") or eq(w, "formats")) return .format;
    if (eq(w, "exec") or eq(w, "do") or eq(w, "run")) return .exec;
    if (eq(w, "undo") or eq(w, "u")) return .undo;
    if (eq(w, "redo") or eq(w, "r")) return .redo;
    if (eq(w, "reset") or eq(w, "revert")) return .reset;
    if (eq(w, "drop") or eq(w, "close") or eq(w, "forget")) return .drop;
    if (eq(w, "clear") or eq(w, "cls")) return .clear;
    if (eq(w, "copy") or eq(w, "yank")) return .copy;
    if (eq(w, "paste")) return .paste;
    if (eq(w, "theme") or eq(w, "themes")) return .theme;
    if (eq(w, "status") or eq(w, "info") or eq(w, "image")) return .status;
    if (eq(w, "help") or eq(w, "?") or eq(w, "h")) return .help;
    if (eq(w, "exit") or eq(w, "quit") or eq(w, "q")) return .quit;
    // Server connections.
    if (eq(w, "connect")) return .connect;
    if (eq(w, "disconnect")) return .disconnect;
    if (eq(w, "reconnect") or eq(w, "refresh")) return .reconnect;
    if (eq(w, "connections") or eq(w, "servers")) return .connections;
    if (eq(w, "projects") or eq(w, "ls")) return .projects;
    if (eq(w, "project-color") or eq(w, "projectcolor") or eq(w, "pcolor")) return .project_color;
    if (eq(w, "rename") or eq(w, "mv")) return .rename;
    if (eq(w, "expire") or eq(w, "expiry") or eq(w, "ttl")) return .expire;
    if (eq(w, "fetch") or eq(w, "pull")) return .fetch;
    if (eq(w, "sync")) return .sync;
    return null;
}

pub const ActionKind = enum { crop, rotate, filter, layout };

pub const Action = struct {
    kind: ActionKind,
    arg: []const u8, // crop spec | rotate count | filter mode | layout source
};

// Map a transform keyword + its argument to an Action, or null when `word` is not one.
pub fn actionOf(word: []const u8, arg: []const u8) ?Action {
    const eq = eqIgnoreCase;
    if (eq(word, "crop")) return .{ .kind = .crop, .arg = arg };
    if (eq(word, "rotate") or eq(word, "rot") or eq(word, "turn")) return .{ .kind = .rotate, .arg = arg };
    if (eq(word, "filter")) return .{ .kind = .filter, .arg = arg };
    if (eq(word, "tint") or eq(word, "color") or eq(word, "colour")) return .{ .kind = .filter, .arg = arg };
    if (eq(word, "bw") or eq(word, "b&w") or eq(word, "grayscale") or eq(word, "greyscale") or eq(word, "gray") or eq(word, "grey")) return .{ .kind = .filter, .arg = "bw" };
    if (eq(word, "sepia")) return .{ .kind = .filter, .arg = "sepia" };
    if (eq(word, "invert")) return .{ .kind = .filter, .arg = "invert" };
    if (eq(word, "contour")) return .{ .kind = .filter, .arg = "contour" };
    if (eq(word, "none")) return .{ .kind = .filter, .arg = "none" };
    if (eq(word, "apply") or eq(word, "draw")) return .{ .kind = .layout, .arg = arg };
    return null;
}

// `exec <action> <args>`: the first word selects the action, the rest is its argument.
// An unknown leading word is treated as a layout source (a path or URL to a JSON).
pub fn parseAction(arg: []const u8) Action {
    const a = std.mem.trim(u8, arg, " \t");
    const sp = std.mem.indexOfAny(u8, a, " \t");
    const head = if (sp) |i| a[0..i] else a;
    const rest = if (sp) |i| std.mem.trim(u8, a[i + 1 ..], " \t") else "";
    return actionOf(head, rest) orelse .{ .kind = .layout, .arg = a };
}

fn eqIgnoreCase(a: []const u8, b: []const u8) bool {
    return std.ascii.eqlIgnoreCase(a, b);
}

// `blank: [format] [w h] [color]` — an optional page-format name (case-insensitive, stored
// canonical), or an optional integer pair, followed by an optional colour. A format token
// and explicit dims are mutually exclusive. Returns null only when the tokens are present
// but malformed (e.g. one dimension, format + dims, or junk).
pub fn parseBlank(gpa: std.mem.Allocator, arg: []const u8) ?args.Blank {
    var b = args.Blank{};
    var it = std.mem.tokenizeAny(u8, arg, " \t");
    var first = it.next();
    if (first == null) return b; // no args → default page, white

    // An optional leading page-format name ("a4", "B5", …).
    if (core.canonicalPageFormat(first.?)) |name| {
        b.page = name;
        first = it.next() orelse return b;
    }

    // If the (next) token is a number, a width/height pair is required.
    if (std.fmt.parseInt(u32, first.?, 10)) |w| {
        if (b.page != null) return null; // a format names the size — no explicit dims with it
        const second = it.next() orelse return null;
        const h = std.fmt.parseInt(u32, second, 10) catch return null;
        b.width = w;
        b.height = h;
        if (it.next()) |color| {
            if (core.parseColor(gpa, color) == null) return null;
            b.color = color;
        }
    } else |_| {
        // Not a number → it must be a colour, and the only token.
        if (core.parseColor(gpa, first.?) == null) return null;
        b.color = first.?;
        if (it.next() != null) return null; // trailing junk after the colour
    }
    return b;
}

// Pull a standalone "album" / "--album" token out of a crop spec, setting `album`. The
// core crop parser only accepts key=value triples, so the modifier must be removed first.
pub fn stripAlbum(gpa: std.mem.Allocator, spec: []const u8, album: *bool) ![]u8 {
    var out: std.ArrayList(u8) = .empty;
    errdefer out.deinit(gpa);
    var it = std.mem.tokenizeAny(u8, spec, " \t");
    while (it.next()) |tok| {
        if (eqIgnoreCase(tok, "album") or eqIgnoreCase(tok, "--album")) {
            album.* = true;
            continue;
        }
        if (out.items.len != 0) try out.append(gpa, ' ');
        try out.appendSlice(gpa, tok);
    }
    return out.toOwnedSlice(gpa);
}

// The project's base name for a `/layout` export: the file name of `label` (after the last
// '/' or '\\') with its extension dropped, falling back to "layout" when empty.
pub fn projectBaseName(label: []const u8) []const u8 {
    var name = label;
    if (std.mem.lastIndexOfAny(u8, name, "/\\")) |i| name = name[i + 1 ..];
    if (std.mem.lastIndexOfScalar(u8, name, '.')) |i| name = name[0..i];
    if (name.len == 0) return "layout";
    return name;
}

// Resolve where `/layout [arg]` writes its JSON (caller owns the returned path):
//   - arg ends with ".json" (case-insensitive) → exactly that path.
//   - arg non-empty without ".json"            → "<arg>/<name>.json" (no doubled '/').
//   - arg empty                                → "<name>.json" in the cwd.
pub fn layoutTarget(gpa: std.mem.Allocator, arg: []const u8, name: []const u8) ![]u8 {
    const a = std.mem.trim(u8, arg, " \t");
    if (a.len == 0) return std.fmt.allocPrint(gpa, "{s}.json", .{name});
    if (std.ascii.endsWithIgnoreCase(a, ".json")) return gpa.dupe(u8, a);
    const sep: []const u8 = if (a[a.len - 1] == '/') "" else "/";
    return std.fmt.allocPrint(gpa, "{s}{s}{s}.json", .{ a, sep, name });
}

const testing = std.testing;

test "parseCommand: slash prefix, bare words, verb + arg split" {
    const up = parseCommand("/upload photo.png");
    try testing.expectEqualStrings("upload", up.word);
    try testing.expectEqualStrings("photo.png", up.arg);

    const up2 = parseCommand("  open   https://x/a.png  ");
    try testing.expectEqualStrings("open", up2.word);
    try testing.expectEqualStrings("https://x/a.png", up2.arg);

    const rot = parseCommand("/rotate -1");
    try testing.expectEqualStrings("rotate", rot.word);
    try testing.expectEqualStrings("-1", rot.arg);

    try testing.expectEqualStrings("redo", parseCommand("/redo").word);
    try testing.expectEqualStrings("", parseCommand("/redo").arg);
    try testing.expectEqualStrings("", parseCommand("   ").word);
    try testing.expectEqualStrings("", parseCommand("/").word);
}

test "verbOf / actionOf: session verbs vs transforms" {
    try testing.expect(verbOf("exit").? == .quit);
    try testing.expect(verbOf("q").? == .quit);
    try testing.expect(verbOf("undo").? == .undo);
    try testing.expect(verbOf("reset").? == .reset);
    try testing.expect(verbOf("drop").? == .drop);
    try testing.expect(verbOf("clear").? == .clear);
    try testing.expect(verbOf("paste").? == .paste);
    try testing.expect(verbOf("theme").? == .theme);
    try testing.expect(verbOf("crop") == null); // a transform, not a session verb
    try testing.expect(verbOf("frob") == null);

    // Server-connection verbs.
    try testing.expect(verbOf("connect").? == .connect);
    try testing.expect(verbOf("disconnect").? == .disconnect);
    try testing.expect(verbOf("reconnect").? == .reconnect);
    try testing.expect(verbOf("refresh").? == .reconnect);
    try testing.expect(verbOf("connections").? == .connections);
    try testing.expect(verbOf("servers").? == .connections);
    try testing.expect(verbOf("projects").? == .projects);
    try testing.expect(verbOf("ls").? == .projects);
    try testing.expect(verbOf("project-color").? == .project_color);
    try testing.expect(verbOf("pcolor").? == .project_color);
    try testing.expect(verbOf("fetch").? == .fetch);
    try testing.expect(verbOf("sync").? == .sync);

    // /format is a session verb (page format), not a transform.
    try testing.expect(verbOf("format").? == .format);
    try testing.expect(verbOf("formats").? == .format);
    try testing.expect(verbOf("formula").? == .formula); // and doesn't shadow /formula

    try testing.expect(actionOf("crop", "x1=0").?.kind == .crop);
    try testing.expect(actionOf("rotate", "-1").?.kind == .rotate);
    try testing.expectEqualStrings("sepia", actionOf("sepia", "").?.arg);
    try testing.expectEqualStrings("invert", actionOf("invert", "").?.arg);
    try testing.expectEqualStrings("contour", actionOf("Contour", "").?.arg);
    try testing.expect(actionOf("invert", "").?.kind == .filter);
    try testing.expect(actionOf("apply", "n.json").?.kind == .layout);
    try testing.expect(actionOf("draw", "n.json").?.kind == .layout);
    try testing.expect(actionOf("frob", "") == null);

    // `layout` is now an export verb, not an alias of the draw action.
    try testing.expect(verbOf("layout").? == .layout);
    try testing.expect(verbOf("savelayout").? == .layout);
    try testing.expect(verbOf("exportlayout").? == .layout);
    try testing.expect(actionOf("layout", "n.json") == null);
}

test "projectBaseName: basename without extension, fallback to layout" {
    try testing.expectEqualStrings("photo", projectBaseName("photo.png"));
    try testing.expectEqualStrings("photo", projectBaseName("/a/b/photo.png"));
    try testing.expectEqualStrings("photo", projectBaseName("a\\b\\photo.png")); // Windows-style separators
    try testing.expectEqualStrings("photo.tar", projectBaseName("photo.tar.gz")); // drop only the last extension
    try testing.expectEqualStrings("blank", projectBaseName("blank"));
    try testing.expectEqualStrings("layout", projectBaseName(""));
    try testing.expectEqualStrings("layout", projectBaseName("/a/b/.png")); // empty base after dropping ext
}

test "layoutTarget: .json passthrough, directory join, bare default" {
    const a = testing.allocator;

    // Ends with .json (any case) → exactly that path.
    const t1 = try layoutTarget(a, "out.json", "photo");
    defer a.free(t1);
    try testing.expectEqualStrings("out.json", t1);
    const t1b = try layoutTarget(a, "Sub/Out.JSON", "photo");
    defer a.free(t1b);
    try testing.expectEqualStrings("Sub/Out.JSON", t1b);

    // Non-empty without .json → treated as a directory/prefix.
    const t2 = try layoutTarget(a, "dir", "photo");
    defer a.free(t2);
    try testing.expectEqualStrings("dir/photo.json", t2);
    const t2b = try layoutTarget(a, "dir/", "photo"); // no doubled slash
    defer a.free(t2b);
    try testing.expectEqualStrings("dir/photo.json", t2b);

    // Empty → "<name>.json" in the cwd.
    const t3 = try layoutTarget(a, "", "photo");
    defer a.free(t3);
    try testing.expectEqualStrings("photo.json", t3);
}

test "parseAction: exec dispatch and layout fallback" {
    const crop = parseAction("crop x1=10% x2=90%");
    try testing.expect(crop.kind == .crop);
    try testing.expectEqualStrings("x1=10% x2=90%", crop.arg);

    try testing.expectEqualStrings("bw", parseAction("bw").arg);
    try testing.expect(parseAction("grayscale").kind == .filter);

    const lay = parseAction("notes.json");
    try testing.expect(lay.kind == .layout);
    try testing.expectEqualStrings("notes.json", lay.arg);

    const lay2 = parseAction("apply https://x/l.json");
    try testing.expect(lay2.kind == .layout);
    try testing.expectEqualStrings("https://x/l.json", lay2.arg);
}

test "parseBlank: dims, colour, and rejects" {
    const a = testing.allocator;
    const b1 = parseBlank(a, "800 600 red").?;
    try testing.expectEqual(@as(u32, 800), b1.width.?);
    try testing.expectEqual(@as(u32, 600), b1.height.?);
    try testing.expectEqualStrings("red", b1.color);

    const b2 = parseBlank(a, "").?;
    try testing.expect(b2.width == null);
    try testing.expectEqualStrings("white", b2.color);

    const b3 = parseBlank(a, "blue").?;
    try testing.expect(b3.width == null);
    try testing.expectEqualStrings("blue", b3.color);

    try testing.expect(parseBlank(a, "800") == null); // lone dimension
    try testing.expect(parseBlank(a, "800 600 notacolour") == null);
    try testing.expect(parseBlank(a, "red extra") == null);
}

test "parseBlank: leading page-format token, canonical + exclusive with dims" {
    const a = testing.allocator;
    const b1 = parseBlank(a, "b5").?;
    try testing.expectEqualStrings("B5", b1.page.?);
    try testing.expect(b1.width == null);
    try testing.expectEqualStrings("white", b1.color);

    const b2 = parseBlank(a, "A5 pink").?;
    try testing.expectEqualStrings("A5", b2.page.?);
    try testing.expectEqualStrings("pink", b2.color);

    const b3 = parseBlank(a, "800 600 red").?; // dims still work without a format
    try testing.expect(b3.page == null);
    try testing.expectEqual(@as(u32, 800), b3.width.?);

    try testing.expect(parseBlank(a, "b5 800 600") == null); // format + dims are exclusive
    try testing.expect(parseBlank(a, "b5 notacolour") == null);
    try testing.expect(parseBlank(a, "b5 red extra") == null);
}

test "stripAlbum: removes the modifier and sets the flag" {
    const a = testing.allocator;
    var album = false;
    const s1 = try stripAlbum(a, "x1=0 x2=100px album", &album);
    defer a.free(s1);
    try testing.expect(album);
    try testing.expectEqualStrings("x1=0 x2=100px", s1);

    album = false;
    const s2 = try stripAlbum(a, "x1=0 x2=100px", &album);
    defer a.free(s2);
    try testing.expect(!album);
    try testing.expectEqualStrings("x1=0 x2=100px", s2);
}

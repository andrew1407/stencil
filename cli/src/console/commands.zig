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
/// Whether the line was typed as a command, slash and all — the one way a secret word answers.
pub fn slashed(line: []const u8) bool {
    const s = std.mem.trim(u8, line, " \t\r\n");
    return s.len != 0 and s[0] == '/';
}

pub fn parseCommand(line: []const u8) Command {
    var s = std.mem.trim(u8, line, " \t\r\n");
    if (s.len != 0 and s[0] == '/') s = std.mem.trimStart(u8, s[1..], " \t");
    if (s.len == 0) return .{ .word = "", .arg = "" };
    if (std.mem.indexOfAny(u8, s, " \t")) |sp| {
        return .{ .word = s[0..sp], .arg = std.mem.trim(u8, s[sp + 1 ..], " \t") };
    }
    return .{ .word = s, .arg = "" };
}

const verbs = @import("commands/verbs.zig");
pub const Verb = verbs.Verb;
pub const verbOf = verbs.verbOf;

pub const ActionKind = enum { crop, rotate, flip, filter, layout };

pub const Action = struct {
    kind: ActionKind,
    arg: []const u8, // crop spec | rotate count | (flip: none) | filter mode | layout source
};

// Map a transform keyword + its argument to an Action, or null when `word` is not one.
pub fn actionOf(word: []const u8, arg: []const u8) ?Action {
    const eq = eqIgnoreCase;
    if (eq(word, "crop")) return .{ .kind = .crop, .arg = arg };
    if (eq(word, "rotate") or eq(word, "rot") or eq(word, "turn")) return .{ .kind = .rotate, .arg = arg };
    if (eq(word, "flip") or eq(word, "mirror")) return .{ .kind = .flip, .arg = arg };
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

// `blank: [format] [w h] [color]` — a page-format name or an integer pair, never both, then an
// optional colour. Null only when the tokens are present but malformed.
pub fn parseBlank(arg: []const u8) ?args.Blank {
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
            if (core.parseColor(core.zstr(color) orelse "") == null) return null;
            b.color = color;
        }
    } else |_| {
        // Not a number → it must be a colour, and the only token.
        if (core.parseColor(core.zstr(first.?) orelse "") == null) return null;
        b.color = first.?;
        if (it.next() != null) return null; // trailing junk after the colour
    }
    return b;
}

// One `/connect` argument pair: a server URL plus its optional access token.
pub const ConnectArg = struct { url: []const u8, token: ?[]const u8 = null };

// A `/connect` word that reads as a server URL rather than a token: it has a scheme,
// a dot or port, or names localhost. Session/admin tokens carry none of those.
pub fn looksLikeServerUrl(word: []const u8) bool {
    if (std.mem.indexOf(u8, word, "://") != null) return true;
    if (std.mem.indexOfAny(u8, word, ".:") != null) return true;
    return eqIgnoreCase(word, "localhost");
}

// Split `/connect` arguments into url/token pairs: each URL-looking word starts a connection and
// one following token word attaches to it. Slices into `arg`; caller owns the returned slice.
pub fn parseConnectArgs(gpa: std.mem.Allocator, arg: []const u8) ![]ConnectArg {
    var out: std.ArrayList(ConnectArg) = .empty;
    errdefer out.deinit(gpa);
    var it = std.mem.tokenizeAny(u8, arg, " ,\t");
    while (it.next()) |word| {
        const last = if (out.items.len != 0) &out.items[out.items.len - 1] else null;
        if (last != null and last.?.token == null and !looksLikeServerUrl(word)) {
            last.?.token = word;
        } else {
            try out.append(gpa, .{ .url = word });
        }
    }
    return out.toOwnedSlice(gpa);
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

// Where `/layout [arg]` writes its JSON (caller owns the path): an `arg` ending ".json" is taken
// as-is, a non-empty one as "<arg>/<name>.json", an empty one as "<name>.json" in the cwd.
pub fn layoutTarget(gpa: std.mem.Allocator, arg: []const u8, name: []const u8) ![]u8 {
    const a = std.mem.trim(u8, arg, " \t");
    if (a.len == 0) return gpa.print("{s}.json", .{name});
    if (std.ascii.endsWithIgnoreCase(a, ".json")) return gpa.dupe(u8, a);
    const sep: []const u8 = if (a[a.len - 1] == '/') "" else "/";
    return gpa.print("{s}{s}{s}.json", .{ a, sep, name });
}

test {
    _ = verbs;
}

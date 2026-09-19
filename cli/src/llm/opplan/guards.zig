//! The §10 user-echo security rules: a plan may only open a path or a URL the USER
//! wrote, in a format this app opens. The model can never introduce a file or a host —
//! naming one it invented is a plan error, not a skipped action.
const std = @import("std");
const testing = std.testing;
const wire = @import("../wire.zig");
const Turn = wire.Turn;
const validate = @import("validate.zig");
const mustPlan = validate.mustPlan;
const mustReject = validate.mustReject;

/// The read scope: only the formats the app itself opens. Extension-based on purpose — the
/// model never gets to hand us an arbitrary file to slurp.
pub fn understoodPath(path: []const u8) bool {
    const dot = std.mem.lastIndexOfScalar(u8, path, '.') orelse return false;
    if (std.mem.lastIndexOfAny(u8, path, "/\\")) |slash| {
        if (dot < slash) return false; // the dot is in a directory name
    }
    const ext = path[dot + 1 ..];
    for (understood_exts) |known| {
        if (std.ascii.eqlIgnoreCase(ext, known)) return true;
    }
    return false;
}

/// Image, video, layout and project extensions — the CLI's own input set.
pub const understood_exts = [_][]const u8{
    "png",     "jpg", "jpeg", "bmp", "tga", "gif",  "webp",
    "mp4",     "mov", "m4v",  "avi", "mkv", "webm", "json",
    "stencil",
};

/// §10 openUrl guard: the model may only ECHO the user — true when `url` appears verbatim in the
/// current turn's text or a replayed USER turn (assistant text and fetched content never count).
pub fn urlEchoedByUser(history: []const Turn, current_text: []const u8, url: []const u8) bool {
    if (std.mem.indexOf(u8, current_text, url) != null) return true;
    for (history) |t| {
        if (t.role == .user and std.mem.indexOf(u8, t.text, url) != null) return true;
    }
    return false;
}

/// The same guard for a LOCAL path (`openFile`, a `save` destination): the path itself or a FOLDER it
/// sits in ("save it to ~/Downloads"); a `..` anywhere voids the grant.
pub fn pathEchoedByUser(history: []const Turn, current_text: []const u8, path: []const u8) bool {
    if (urlEchoedByUser(history, current_text, path)) return true;
    if (std.mem.indexOf(u8, path, "..") != null) return false;
    var end = path.len;
    while (std.mem.lastIndexOfScalar(u8, path[0..end], '/')) |slash| {
        if (slash == 0) return false; // "/" alone grants nothing
        if (folderNamedByUser(history, current_text, path[0..slash])) return true;
        end = slash;
    }
    return false;
}

/// True when the user wrote `dir` as a path of its OWN, not merely as the head of a longer one:
/// without that, naming one file would hand the model the whole folder it sits in.
pub fn folderNamedByUser(history: []const Turn, current_text: []const u8, dir: []const u8) bool {
    if (namedAsPathIn(current_text, dir)) return true;
    for (history) |t| {
        if (t.role == .user and namedAsPathIn(t.text, dir)) return true;
    }
    return false;
}

/// `needle` appears in `text` as a complete path token: at the end, or followed by whitespace
/// or sentence punctuation — never by another path segment.
pub fn namedAsPathIn(text: []const u8, needle: []const u8) bool {
    var i: usize = 0;
    while (std.mem.indexOfPos(u8, text, i, needle)) |at| : (i = at + 1) {
        const after = at + needle.len;
        if (after >= text.len) return true;
        const c = text[after];
        if (std.ascii.isWhitespace(c)) return true;
        switch (c) {
            ',', ';', ':', '"', '\'', ')', ']', '}', '!', '?' => return true,
            // A dot only ends the token when it ends the sentence, so "~/Downloads.png"
            // (a file the user named) never grants the "~/Downloads" folder.
            '.' => if (after + 1 >= text.len or std.ascii.isWhitespace(text[after + 1])) return true,
            else => {},
        }
    }
    return false;
}

test "urlEchoedByUser: only the USER's own turns count, verbatim (§10)" {
    const url = "https://a.example/cat.png";
    const user_turn = [_]Turn{.{ .role = .user, .text = "load https://a.example/cat.png please" }};
    const assistant_turn = [_]Turn{.{ .role = .assistant, .text = "try https://a.example/cat.png" }};
    // The current turn or an earlier USER turn may carry the URL…
    try testing.expect(urlEchoedByUser(&.{}, "open https://a.example/cat.png", url));
    try testing.expect(urlEchoedByUser(&user_turn, "crop it", url));
    // …but assistant text never authorises one, and a rewritten/completed URL is not an echo.
    try testing.expect(!urlEchoedByUser(&assistant_turn, "crop it", url));
    try testing.expect(!urlEchoedByUser(&.{}, "load https://a.example/cat", url));
    try testing.expect(!urlEchoedByUser(&.{}, "", url));
}

test "openFile: a local path in a format we open, never a URL or an unknown type" {
    var ok = try mustPlan("{\"reply\":\"ok\",\"actions\":[{\"op\":\"openFile\",\"path\":\"~/Pictures/a.png\"}]}");
    defer ok.deinit();
    try testing.expectEqualStrings("~/Pictures/a.png", ok.actions[0].open_file.path);

    // Every format the console itself opens is allowed…
    inline for (.{ "a.jpg", "clip.mp4", "notes.json", "p.stencil", "/tmp/x.WEBP" }) |path| {
        var p = try mustPlan("{\"reply\":\"ok\",\"actions\":[{\"op\":\"openFile\",\"path\":\"" ++ path ++ "\"}]}");
        defer p.deinit();
        try testing.expectEqualStrings(path, p.actions[0].open_file.path);
    }
    // …and nothing else: no URL (openUrl owns those), no directory, no arbitrary file type.
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"openFile\",\"path\":\"https://x.example/a.png\"}]}",
        "invalid openFile action: \"path\" must be a local value, not a URL",
    );
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"openFile\",\"path\":\"~/Documents\"}]}",
        "invalid openFile action: \"~/Documents\" is not an image, video, .json layout or .stencil project",
    );
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"openFile\",\"path\":\"~/.ssh/id_rsa\"}]}",
        "invalid openFile action: \"~/.ssh/id_rsa\" is not an image, video, .json layout or .stencil project",
    );
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"openFile\",\"path\":\"\"}]}",
        "invalid openFile action: \"path\" must be a non-empty string",
    );
}

test "pathEchoedByUser: only a path the user wrote counts, wherever it was written" {
    const hist = [_]Turn{
        .{ .role = .user, .text = "load ~/Pictures/portrait.png please" },
        .{ .role = .assistant, .text = "sure — /etc/passwd is also readable" }, // never counts
    };
    try testing.expect(pathEchoedByUser(&hist, "crop it", "~/Pictures/portrait.png"));
    try testing.expect(pathEchoedByUser(&hist, "save into ~/Downloads", "~/Downloads"));
    try testing.expect(!pathEchoedByUser(&hist, "crop it", "/etc/passwd"));
    try testing.expect(!pathEchoedByUser(&hist, "crop it", "~/Pictures/other.png"));
}

test "pathEchoedByUser: naming a FOLDER grants the files inside it, but never a way out" {
    const hist = [_]Turn{.{ .role = .user, .text = "put the results in /Users/me/Downloads" }};
    // How people actually ask: the folder is theirs, the file name is the model's.
    try testing.expect(pathEchoedByUser(&hist, "go on", "/Users/me/Downloads/portrait-bw.png"));
    try testing.expect(pathEchoedByUser(&hist, "go on", "/Users/me/Downloads/sub/dir/x.stencil"));
    // A sibling folder was never given, and `..` voids the grant it would climb out of.
    try testing.expect(!pathEchoedByUser(&hist, "go on", "/Users/me/Documents/x.png"));
    try testing.expect(!pathEchoedByUser(&hist, "go on", "/Users/me/Downloads/../.ssh/id_rsa"));
    // A bare "/" grants nothing, however the model spells it.
    try testing.expect(!pathEchoedByUser(&[_]Turn{.{ .role = .user, .text = "save to /" }}, "", "/etc/hosts"));
}

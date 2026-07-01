//! URL fetch using Zig's own HTTP client (std.http.Client) — including HTTPS, via Zig's
//! built-in TLS and system CA bundle. No external tool: images and layout JSON given as
//! http(s) URLs are downloaded in-process. (Video URLs are handled by ffmpeg, which reads
//! URLs directly; pure-Zig video decoding isn't practical — see video.zig.)
const std = @import("std");

pub const Error = error{HttpFailed};

pub fn isUrl(s: []const u8) bool {
    return std.ascii.startsWithIgnoreCase(s, "http://") or
        std.ascii.startsWithIgnoreCase(s, "https://");
}

/// True when `s` carries a URL scheme (`scheme://…`) OTHER than http/https — e.g.
/// `ftp://`, `file://`, `rtmp://`. These must never reach ffmpeg (whose protocol surface is
/// far wider than this in-process http(s) client), so the pipeline rejects them up front.
/// A bare local path (no scheme) or an http(s) URL returns false.
pub fn hasForeignScheme(s: []const u8) bool {
    if (isUrl(s)) return false;
    const sep = std.mem.indexOf(u8, s, "://") orelse return false;
    if (sep == 0) return false;
    // Only treat the prefix as a scheme when it is ALPHA *( ALPHA / DIGIT / "+" / "-" / "." ),
    // so a path that merely contains "://" is not misread as a foreign URL.
    for (s[0..sep], 0..) |c, i| {
        const ok = std.ascii.isAlphabetic(c) or
            (i > 0 and (std.ascii.isDigit(c) or c == '+' or c == '-' or c == '.'));
        if (!ok) return false;
    }
    return true;
}

/// GET `url`, returning the owned response body bytes.
pub fn fetch(gpa: std.mem.Allocator, io: std.Io, url: []const u8) ![]u8 {
    var client: std.http.Client = .{ .allocator = gpa, .io = io };
    defer client.deinit();

    var body: std.Io.Writer.Allocating = .init(gpa);
    defer body.deinit();

    const result = client.fetch(.{
        .location = .{ .url = url },
        .response_writer = &body.writer,
    }) catch |e| {
        std.debug.print("error: HTTP request failed for {s}: {s}\n", .{ url, @errorName(e) });
        return Error.HttpFailed;
    };

    const code = @intFromEnum(result.status);
    if (code < 200 or code >= 300) {
        std.debug.print("error: HTTP {d} fetching {s}\n", .{ code, url });
        return Error.HttpFailed;
    }
    return gpa.dupe(u8, body.written());
}

const testing = std.testing;

test "isUrl" {
    try testing.expect(isUrl("https://example.com/a.png"));
    try testing.expect(isUrl("HTTP://x"));
    try testing.expect(!isUrl("/local/path.png"));
}

test "hasForeignScheme" {
    // Foreign schemes are rejected …
    try testing.expect(hasForeignScheme("ftp://host/clip.mp4"));
    try testing.expect(hasForeignScheme("file:///etc/passwd.mp4"));
    try testing.expect(hasForeignScheme("rtmp://host/live"));
    // … while http(s) URLs and bare local paths are not.
    try testing.expect(!hasForeignScheme("https://example.com/v.mp4"));
    try testing.expect(!hasForeignScheme("http://h/v.webm?token=1"));
    try testing.expect(!hasForeignScheme("/home/me/clip.mp4"));
    try testing.expect(!hasForeignScheme("clip.mp4"));
    try testing.expect(!hasForeignScheme("a/b://c")); // not a scheme prefix
}

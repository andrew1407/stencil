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

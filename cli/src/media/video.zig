//! Video frame extraction via the system ffmpeg. The C++ core never touches codecs and
//! pure-Zig video decoding isn't practical, so we shell out: ffmpeg seeks the requested
//! frame and writes a single PNG to stdout, which we capture as bytes for the normal
//! image pipeline. If ffmpeg isn't installed the caller surfaces a clear hint.
const std = @import("std");
const child = @import("../safety/child.zig");
const report = @import("../app/report.zig");
const sanitize = @import("../safety/sanitize.zig");
const mediaTypes = @import("types.zig");

pub const Error = error{ FfmpegMissing, FfmpegFailed };

/// Heuristic: does this path/URL look like a video (by extension)? The list is the shared
/// canon's `surfaces.cli.video` (media/types.zig), not a copy kept here.
pub fn looksLikeVideo(path: []const u8) bool {
    // Trim any URL query/fragment before checking the extension.
    var end = path.len;
    if (std.mem.indexOfAny(u8, path, "?#")) |q| end = q;
    const p = path[0..end];
    for (mediaTypes.videoExts()) |ext| {
        if (p.len >= ext.len and std.ascii.eqlIgnoreCase(p[p.len - ext.len ..], ext)) return true;
    }
    return false;
}

/// Grab frame `frame` of `src` (a local path or URL ffmpeg can read) as PNG bytes.
/// Caller owns the returned slice.
pub fn extractFrame(gpa: std.mem.Allocator, io: std.Io, src: []const u8, frame: u32) ![]u8 {
    var filter_buf: [48]u8 = undefined;
    const select = try std.fmt.bufPrint(&filter_buf, "select=eq(n\\,{d})", .{frame});

    // Constrain ffmpeg's protocol surface (it defaults to file/http/ftp/rtmp/concat/…): a local source only
    // needs `file`, and a remote one is denied `file` so a malicious playlist cannot read local files.
    const remote = std.ascii.startsWithIgnoreCase(src, "http://") or
        std.ascii.startsWithIgnoreCase(src, "https://");
    const whitelist = if (remote) "http,https,tcp,tls,crypto" else "file";

    const argv = [_][]const u8{
        "ffmpeg",              "-nostdin",   "-loglevel", "error",
        "-protocol_whitelist", whitelist,    "-i",        src,
        "-vf",                 select,       "-frames:v", "1",
        "-f",                  "image2pipe", "-vcodec",   "png",
        "-",
    };

    const res = child.run(gpa, io, .{ .argv = &argv }) catch |e| switch (e) {
        error.FileNotFound => return Error.FfmpegMissing,
        else => return e,
    };
    defer gpa.free(res.stderr);
    errdefer gpa.free(res.stdout);

    const ok = switch (res.term) {
        .exited => |code| code == 0,
        else => false,
    };
    if (!ok or res.stdout.len == 0) {
        if (res.stderr.len > 0) {
            // ffmpeg echoes the URL it was given, token and all, so its prose is untrusted.
            var buf: sanitize.DetailBuf = undefined;
            report.err("ffmpeg: {s}\n", .{sanitize.sanitizeDetail(res.stderr, &buf)});
        }
        gpa.free(res.stdout);
        return Error.FfmpegFailed;
    }
    return res.stdout;
}

const testing = std.testing;

test "looksLikeVideo by extension, ignoring URL query" {
    try testing.expect(looksLikeVideo("clip.MP4"));
    try testing.expect(looksLikeVideo("https://h/v.webm?token=1"));
    try testing.expect(!looksLikeVideo("photo.png"));
}

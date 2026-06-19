//! Video frame extraction via the system ffmpeg. The C++ core never touches codecs and
//! pure-Zig video decoding isn't practical, so we shell out: ffmpeg seeks the requested
//! frame and writes a single PNG to stdout, which we capture as bytes for the normal
//! image pipeline. If ffmpeg isn't installed the caller surfaces a clear hint.
const std = @import("std");

pub const Error = error{ FfmpegMissing, FfmpegFailed };

const video_exts = [_][]const u8{
    ".mp4", ".mov", ".mkv", ".webm", ".avi", ".m4v", ".mpg", ".mpeg", ".wmv", ".flv", ".ts", ".gifv",
};

/// Heuristic: does this path/URL look like a video (by extension)?
pub fn looksLikeVideo(path: []const u8) bool {
    // Trim any URL query/fragment before checking the extension.
    var end = path.len;
    if (std.mem.indexOfAny(u8, path, "?#")) |q| end = q;
    const p = path[0..end];
    for (video_exts) |ext| {
        if (p.len >= ext.len and std.ascii.eqlIgnoreCase(p[p.len - ext.len ..], ext)) return true;
    }
    return false;
}

/// Grab frame `frame` of `src` (a local path or URL ffmpeg can read) as PNG bytes.
/// Caller owns the returned slice.
pub fn extractFrame(gpa: std.mem.Allocator, io: std.Io, src: []const u8, frame: u32) ![]u8 {
    var filter_buf: [48]u8 = undefined;
    const select = try std.fmt.bufPrint(&filter_buf, "select=eq(n\\,{d})", .{frame});

    const argv = [_][]const u8{
        "ffmpeg",     "-nostdin", "-loglevel", "error",
        "-i",         src,        "-vf",       select,
        "-frames:v",  "1",        "-f",        "image2pipe",
        "-vcodec",    "png",      "-",
    };

    const res = std.process.run(gpa, io, .{ .argv = &argv }) catch |e| switch (e) {
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
        if (res.stderr.len > 0) std.debug.print("ffmpeg: {s}\n", .{res.stderr});
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

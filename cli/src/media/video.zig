//! Video through the system ffmpeg. The C++ core never touches codecs and pure-Zig video
//! decoding isn't practical, so we shell out: ffmpeg seeks the requested frame and writes a
//! single PNG to stdout for the normal image pipeline, and ffprobe reads a stream's size,
//! duration and frame count. A missing tool is its own error, so the caller can hint.
const std = @import("std");
const child = @import("../safety/child.zig");
const report = @import("../app/report.zig");
const sanitize = @import("../safety/sanitize.zig");
const mediaTypes = @import("types.zig");

pub const Error = error{ FfmpegMissing, FfmpegFailed, FfprobeMissing, FfprobeFailed };

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

    const argv = [_][]const u8{
        "ffmpeg",              "-nostdin",     "-loglevel", "error",
        "-protocol_whitelist", whitelist(src), "-i",        src,
        "-vf",                 select,         "-frames:v", "1",
        "-f",                  "image2pipe",   "-vcodec",   "png",
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

/// Constrain ffmpeg's protocol surface (it defaults to file/http/ftp/rtmp/concat/…): a local source only
/// needs `file`, and a remote one is denied `file` so a malicious playlist cannot read local files.
fn whitelist(src: []const u8) []const u8 {
    const remote = std.ascii.startsWithIgnoreCase(src, "http://") or
        std.ascii.startsWithIgnoreCase(src, "https://");
    return if (remote) "http,https,tcp,tls,crypto" else "file";
}

/// A video's first stream: pixel size, and what the container says of its length.
pub const Stream = struct { width: u32, height: u32, duration_ms: ?u64 = null, frames: ?u64 = null };

/// Read `src`'s first video stream with ffprobe. `FfprobeMissing` when it is not on PATH.
pub fn probe(gpa: std.mem.Allocator, io: std.Io, src: []const u8) !Stream {
    const argv = [_][]const u8{
        "ffprobe",             "-v",            "error",
        "-protocol_whitelist", whitelist(src),  "-select_streams",
        "v:0",                 "-show_entries", "stream=width,height,nb_frames,avg_frame_rate,duration:format=duration",
        "-of",                 "json",          src,
    };
    const res = child.run(gpa, io, .{ .argv = &argv, .stdout_limit = .limited(1 << 20) }) catch |e| switch (e) {
        error.FileNotFound => return Error.FfprobeMissing,
        else => return e,
    };
    defer gpa.free(res.stdout);
    defer gpa.free(res.stderr);
    const ok = switch (res.term) {
        .exited => |code| code == 0,
        else => false,
    };
    if (!ok) {
        var buf: sanitize.DetailBuf = undefined;
        report.err("ffprobe: {s}\n", .{sanitize.sanitizeDetail(res.stderr, &buf)});
        return Error.FfprobeFailed;
    }
    return parseProbe(gpa, res.stdout) orelse Error.FfprobeFailed;
}

/// ffprobe's JSON: the stream's size, a duration from the stream else the container, and a frame
/// count from `nb_frames` else estimated as duration × `avg_frame_rate` (webm stores none).
pub fn parseProbe(gpa: std.mem.Allocator, json: []const u8) ?Stream {
    const T = struct {
        streams: []const struct {
            width: u32 = 0,
            height: u32 = 0,
            nb_frames: []const u8 = "",
            avg_frame_rate: []const u8 = "",
            duration: []const u8 = "",
        } = &.{},
        format: struct { duration: []const u8 = "" } = .{},
    };
    var p = std.json.parseFromSlice(T, gpa, json, .{ .ignore_unknown_fields = true }) catch return null;
    defer p.deinit();
    if (p.value.streams.len == 0) return null;
    const s = p.value.streams[0];
    if (s.width == 0 or s.height == 0) return null;
    const seconds = number(s.duration) orelse number(p.value.format.duration);
    var out = Stream{ .width = s.width, .height = s.height };
    if (seconds) |d| out.duration_ms = @intFromFloat(@round(d * 1000));
    out.frames = if (number(s.nb_frames)) |n| @intFromFloat(n) else if (seconds) |d| estimate(d, s.avg_frame_rate) else null;
    return out;
}

fn number(s: []const u8) ?f64 {
    const v = std.fmt.parseFloat(f64, s) catch return null;
    return if (std.math.isFinite(v) and v >= 0) v else null;
}

/// `num/den` frames per second times the duration, rounded; null for a rate of 0/0.
fn estimate(seconds: f64, rate: []const u8) ?u64 {
    const slash = std.mem.indexOfScalar(u8, rate, '/') orelse return null;
    const num = number(rate[0..slash]) orelse return null;
    const den = number(rate[slash + 1 ..]) orelse return null;
    if (num == 0 or den == 0) return null;
    return @intFromFloat(@round(seconds * num / den));
}

const testing = std.testing;

test "looksLikeVideo by extension, ignoring URL query" {
    try testing.expect(looksLikeVideo("clip.MP4"));
    try testing.expect(looksLikeVideo("https://h/v.webm?token=1"));
    try testing.expect(!looksLikeVideo("photo.png"));
}

test "parseProbe reads size, duration and frames, estimating frames when none are stored" {
    const a = testing.allocator;
    const mp4 =
        \\{"streams":[{"width":640,"height":360,"avg_frame_rate":"30/1","nb_frames":"150","duration":"5.000000"}],
        \\ "format":{"duration":"5.020000"}}
    ;
    const s = parseProbe(a, mp4).?;
    try testing.expectEqual(@as(u32, 640), s.width);
    try testing.expectEqual(@as(u32, 360), s.height);
    try testing.expectEqual(@as(?u64, 5000), s.duration_ms);
    try testing.expectEqual(@as(?u64, 150), s.frames);

    const webm =
        \\{"streams":[{"width":320,"height":240,"avg_frame_rate":"30000/1001"}],"format":{"duration":"2.002"}}
    ;
    const w = parseProbe(a, webm).?;
    try testing.expectEqual(@as(?u64, 2002), w.duration_ms);
    try testing.expectEqual(@as(?u64, 60), w.frames);

    const bare = parseProbe(a, "{\"streams\":[{\"width\":8,\"height\":8,\"avg_frame_rate\":\"0/0\"}]}").?;
    try testing.expect(bare.duration_ms == null and bare.frames == null);
    try testing.expect(parseProbe(a, "{\"streams\":[]}") == null);
    try testing.expect(parseProbe(a, "not json") == null);
}

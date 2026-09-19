//! Acquiring the bytes a run starts from: a local path, an http(s) URL or a video frame,
//! plus the synthesised blank page. Only http(s) and local paths are accepted — a foreign
//! scheme is refused before anything reaches ffmpeg or the in-process fetcher.
const std = @import("std");
const core = @import("../core.zig");
const image = @import("../image.zig");
const video = @import("../video.zig");
const net = @import("../net.zig");
const args = @import("../args.zig");
const report = @import("../report.zig");
const page_mod = @import("../page.zig");

const MAX_FILE = 256 << 20; // 256 MiB read cap for inputs
const BLANK_MIN = 1;
const BLANK_MAX = 8192;

pub fn loadSource(gpa: std.mem.Allocator, io: std.Io, input: []const u8, frame: u32) ![]u8 {
    // Only http(s) URLs and local paths are accepted: reject any other scheme up front so an `.mp4`-looking
    // `ftp://`/`file://` string can never reach ffmpeg, whose protocol surface is far wider than ours.
    if (net.hasForeignScheme(input)) {
        report.err("unsupported URL scheme in '{s}' — pass an http(s) URL or a local path\n", .{input});
        return error.UnsupportedScheme;
    }
    if (video.looksLikeVideo(input)) {
        return video.extractFrame(gpa, io, input, frame) catch |e| return mapMediaError(e);
    }
    // User-named URL → non-strict (loopback allowed for the user's own dev/fixture server).
    if (net.isUrl(input)) return net.fetch(gpa, io, input, false) catch |e| return mapMediaError(e);
    return readLocal(gpa, io, input);
}

pub fn loadText(gpa: std.mem.Allocator, io: std.Io, src: []const u8) ![]u8 {
    if (net.isUrl(src)) return net.fetch(gpa, io, src, false) catch |e| return mapMediaError(e);
    return readLocal(gpa, io, src);
}

/// Load a layout JSON document (file path or http(s) URL) as raw bytes — for the console's
/// structured model, which records the lines rather than baking them. Prints on failure.
pub fn loadLayoutBytes(gpa: std.mem.Allocator, io: std.Io, src: []const u8) ![]u8 {
    return loadText(gpa, io, src);
}

pub fn readLocal(gpa: std.mem.Allocator, io: std.Io, path: []const u8) ![]u8 {
    const home = try expandHome(gpa, path);
    defer gpa.free(home);
    const dir = std.Io.Dir.cwd();
    return dir.readFileAlloc(io, home, gpa, .limited(MAX_FILE)) catch |e| {
        report.err("cannot read '{s}': {s}\n", .{ home, @errorName(e) });
        return e;
    };
}

/// Expand a leading `~` (bare, or `~/…`) to $HOME: the shell does this for argv, but a path typed INSIDE
/// the console arrives literally, where `~/Downloads/x.png` would mean a directory named "~".
pub fn expandHome(gpa: std.mem.Allocator, path: []const u8) ![]u8 {
    if (!(std.mem.eql(u8, path, "~") or std.mem.startsWith(u8, path, "~/"))) {
        return gpa.dupe(u8, path);
    }
    // libc getenv (like the writes in line_edit): std.posix has none, and the console's
    // environ map does not reach this layer.
    const raw_home = std.c.getenv("HOME") orelse return gpa.dupe(u8, path);
    const home = std.mem.span(raw_home);
    if (home.len == 0) return gpa.dupe(u8, path);
    const rest = path[1..]; // "" for a bare "~", else "/…"
    const base = if (home.len > 1 and home[home.len - 1] == '/') home[0 .. home.len - 1] else home;
    return std.fmt.allocPrint(gpa, "{s}{s}", .{ base, rest });
}

pub fn mapMediaError(e: anyerror) anyerror {
    switch (e) {
        video.Error.FfmpegMissing => report.err("ffmpeg not found on PATH — needed only for video input\n", .{}),
        else => {},
    }
    return e;
}

pub fn acquireBlank(gpa: std.mem.Allocator, blank: args.Blank) !image.Rgba8 {
    // Explicit dims win; else the picked page format's default size.
    var w: i64 = blank.width orelse 0;
    var h: i64 = blank.height orelse 0;
    if (blank.width == null or blank.height == null) {
        const s = page_mod.blankSizeFor(blank.page, 0, 0);
        w = s.w;
        h = s.h;
    }
    w = std.math.clamp(w, BLANK_MIN, BLANK_MAX);
    h = std.math.clamp(h, BLANK_MIN, BLANK_MAX);
    const color = core.parseColor(core.zstr(blank.color) orelse "") orelse core.Rgba{ .r = 255, .g = 255, .b = 255, .a = 255 };

    const uw: usize = @intCast(w);
    const uh: usize = @intCast(h);
    const pixels = try gpa.alloc(u8, uw * uh * 4);
    core.fillRGBA(pixels, @intCast(uw * uh), color);
    return .{ .width = uw, .height = uh, .pixels = pixels };
}

const testing = std.testing;

test "expandHome: a leading ~ becomes $HOME, everything else is untouched" {
    const a = testing.allocator;
    const home = std.mem.span(std.c.getenv("HOME") orelse return error.SkipZigTest);

    const bare = try expandHome(a, "~");
    defer a.free(bare);
    try testing.expectEqualStrings(home, bare);

    const under = try expandHome(a, "~/Downloads/out.png");
    defer a.free(under);
    const want = try std.fmt.allocPrint(a, "{s}/Downloads/out.png", .{home});
    defer a.free(want);
    try testing.expectEqualStrings(want, under);

    // Not a home reference: a relative path, an absolute one, and a NAME that merely starts
    // with a tilde all pass through as typed.
    for ([_][]const u8{ "out.png", "/tmp/out.png", "~tilde/out.png", "sub/~/out.png" }) |path| {
        const same = try expandHome(a, path);
        defer a.free(same);
        try testing.expectEqualStrings(path, same);
    }
}
test "loadSource rejects foreign URL schemes before any IO" {
    const gpa = testing.allocator;
    // hasForeignScheme rejects these up front, so `io` is never touched.
    try testing.expectError(error.UnsupportedScheme, loadSource(gpa, undefined, "file:///etc/passwd.mp4", 0));
    try testing.expectError(error.UnsupportedScheme, loadSource(gpa, undefined, "ftp://host/clip.png", 0));
}

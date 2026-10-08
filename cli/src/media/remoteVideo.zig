//! A video source as ffmpeg and ffprobe see it: a local path as given, or an http(s) URL fetched
//! through net.zig's guard (the host as written and as resolved, redirects refused, the 64 MiB body
//! cap) into a private temp file — so the tools never dial a host and never follow a redirect.
const std = @import("std");
const builtin = @import("builtin");
const net = @import("../net.zig");

/// The path a tool reads; `staged` is the temp copy `deinit` deletes.
pub const Source = struct {
    path: []const u8,
    staged: ?[]u8 = null,

    pub fn deinit(self: *Source, gpa: std.mem.Allocator, io: std.Io) void {
        const p = self.staged orelse return;
        std.Io.Dir.cwd().deleteFile(io, p) catch {};
        gpa.free(p);
        self.staged = null;
    }
};

/// `src` itself when local; else its body, fetched as an image URL is (`strict` also blocks
/// loopback), in an owner-only temp file named for its extension.
pub fn stage(gpa: std.mem.Allocator, io: std.Io, src: []const u8, strict: bool) !Source {
    if (!net.isUrl(src)) return .{ .path = src };
    const bytes = try net.fetch(gpa, io, src, strict);
    defer gpa.free(bytes);

    var rnd: [8]u8 = undefined;
    io.random(&rnd);
    const sep: []const u8 = if (builtin.os.tag == .windows) "\\" else "/";
    const path = try std.fmt.allocPrint(gpa, "{s}{s}stencil_video_{s}{s}", .{ tmpDir(), sep, &std.fmt.bytesToHex(rnd, .lower), extOf(src) });
    errdefer gpa.free(path);
    const perms: std.Io.File.Permissions = if (@hasDecl(std.Io.File.Permissions, "fromMode")) .fromMode(0o600) else .default_file;
    const file = try std.Io.Dir.cwd().createFile(io, path, .{ .exclusive = true, .permissions = perms });
    defer file.close(io);
    file.writeStreamingAll(io, bytes) catch |e| {
        std.Io.Dir.cwd().deleteFile(io, path) catch {};
        return e;
    };
    return .{ .path = path, .staged = path };
}

/// `.ext` of the URL's path (query and fragment trimmed), or "" when it is not plain alphanumerics.
fn extOf(url: []const u8) []const u8 {
    const end = std.mem.indexOfAny(u8, url, "?#") orelse url.len;
    const p = url[0..end];
    const dot = std.mem.lastIndexOfScalar(u8, p, '.') orelse return "";
    const ext = p[dot..];
    if (ext.len < 2 or ext.len > 6) return "";
    for (ext[1..]) |c| if (!std.ascii.isAlphanumeric(c)) return "";
    return ext;
}

/// The per-user temp dir: $TMPDIR (else /tmp), or %TEMP% on Windows.
fn tmpDir() []const u8 {
    if (builtin.os.tag == .windows) {
        const t = std.c.getenv("TEMP") orelse std.c.getenv("TMP") orelse return ".";
        return std.mem.trimEnd(u8, std.mem.span(t), "\\/");
    }
    const t = std.c.getenv("TMPDIR") orelse return "/tmp";
    return std.mem.trimEnd(u8, std.mem.span(t), "/");
}

const testing = std.testing;

test "a local path is read where it lies, and nothing is staged" {
    var s = try stage(testing.allocator, undefined, "clips/a.mp4", false);
    defer s.deinit(testing.allocator, undefined);
    try testing.expectEqualStrings("clips/a.mp4", s.path);
    try testing.expect(s.staged == null);
}

test "extOf keeps a plain extension and drops anything else" {
    try testing.expectEqualStrings(".mp4", extOf("https://h/v/clip.mp4?sig=1#t"));
    try testing.expectEqualStrings(".webm", extOf("http://h/a.webm"));
    try testing.expectEqualStrings("", extOf("http://h/a.mp4;x"));
    try testing.expectEqualStrings("", extOf("http://h/noext"));
}

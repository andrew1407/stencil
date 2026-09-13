//! A `@source` spec -> the concrete inputs it names. The core classifies the spec; opening
//! it is the adapter's job, so directory listing, globbing and the fetch guard live here.
const std = @import("std");

const image = @import("../image.zig");
const net = @import("../net.zig");
const scriptCore = @import("../scriptCore.zig");
const video = @import("../video.zig");

pub const Error = error{ NoSuchSource, ForeignScheme, TooManyInputs };

pub const MAX_INPUTS: usize = 512;

/// One glob segment: `*` any run, `?` one byte, `[abc]` a set. No `**`, and it matches a
/// single path component — a script names a directory, not a tree.
pub fn globMatch(pattern: []const u8, name: []const u8) bool {
    var p: usize = 0;
    var n: usize = 0;
    var star: ?usize = null;
    var starName: usize = 0;
    while (n < name.len) {
        if (p < pattern.len and pattern[p] == '[') {
            const close = std.mem.indexOfScalarPos(u8, pattern, p + 1, ']') orelse return false;
            const set = pattern[p + 1 .. close];
            const negate = set.len > 0 and set[0] == '^';
            const body = if (negate) set[1..] else set;
            const hit = std.mem.indexOfScalar(u8, body, name[n]) != null;
            if (hit != negate) {
                p = close + 1;
                n += 1;
                continue;
            }
        } else if (p < pattern.len and (pattern[p] == '?' or pattern[p] == name[n])) {
            p += 1;
            n += 1;
            continue;
        } else if (p < pattern.len and pattern[p] == '*') {
            star = p;
            starName = n;
            p += 1;
            continue;
        }
        if (star) |s| {
            p = s + 1;
            starName += 1;
            n = starName;
            continue;
        }
        return false;
    }
    while (p < pattern.len and pattern[p] == '*') p += 1;
    return p == pattern.len;
}

const DirLeaf = struct { dir: []const u8, leaf: []const u8 };

fn splitDir(spec: []const u8) DirLeaf {
    const slash = std.mem.lastIndexOfScalar(u8, spec, '/');
    if (slash) |i| return .{ .dir = spec[0 .. i + 1], .leaf = spec[i + 1 ..] };
    return .{ .dir = "./", .leaf = spec };
}

/// A directory or glob picks up only what this build can actually open: the encoder's own
/// formats (plus "jpeg", the spelling stb shares with "jpg") and the video extensions.
fn isMedia(name: []const u8) bool {
    if (video.looksLikeVideo(name)) return true;
    const dot = std.mem.lastIndexOfScalar(u8, name, '.') orelse return false;
    const ext = name[dot + 1 ..];
    if (std.ascii.eqlIgnoreCase(ext, "jpeg")) return true;
    return image.formatFromExt(ext) != null;
}

/// Every input `spec` names, sorted so a directory or glob runs in a stable order. The
/// caller owns the list and each path in it.
pub fn expand(gpa: std.mem.Allocator, io: std.Io, spec: []const u8, kind: scriptCore.SourceKind) ![][]u8 {
    var out: std.ArrayList([]u8) = .empty;
    errdefer {
        for (out.items) |p| gpa.free(p);
        out.deinit(gpa);
    }

    switch (kind) {
        .url, .file => {
            if (kind == .file and net.hasForeignScheme(spec)) return Error.ForeignScheme;
            try out.append(gpa, try gpa.dupe(u8, spec));
        },
        .dir, .glob => {
            const parts: DirLeaf = if (kind == .dir) .{ .dir = spec, .leaf = "*" } else splitDir(spec);
            var dir = std.Io.Dir.cwd().openDir(io, parts.dir, .{ .iterate = true }) catch
                return Error.NoSuchSource;
            defer dir.close(io);
            var walker = try dir.walk(gpa);
            defer walker.deinit();
            while (try walker.next(io)) |entry| {
                if (entry.kind != .file) continue;
                if (std.mem.indexOfAny(u8, entry.path, "/\\") != null) continue; // one level only
                if (entry.basename.len > 0 and entry.basename[0] == '.') continue;
                if (!globMatch(parts.leaf, entry.basename)) continue;
                if (!isMedia(entry.basename)) continue;
                if (out.items.len >= MAX_INPUTS) return Error.TooManyInputs;
                const joined = try std.fs.path.join(gpa, &.{ parts.dir, entry.basename });
                try out.append(gpa, joined);
            }
            std.mem.sort([]u8, out.items, {}, struct {
                fn lt(_: void, a: []u8, b: []u8) bool {
                    return std.mem.lessThan(u8, a, b);
                }
            }.lt);
        },
        .project => {},
    }
    return out.toOwnedSlice(gpa);
}

pub fn freeInputs(gpa: std.mem.Allocator, inputs: [][]u8) void {
    for (inputs) |p| gpa.free(p);
    gpa.free(inputs);
}

test "globMatch handles the one-segment forms" {
    try std.testing.expect(globMatch("*", "a.png"));
    try std.testing.expect(globMatch("a*.png", "ab.png"));
    try std.testing.expect(globMatch("a*.png", "a.png"));
    try std.testing.expect(!globMatch("a*.png", "b.png"));
    try std.testing.expect(globMatch("?.png", "a.png"));
    try std.testing.expect(!globMatch("?.png", "ab.png"));
    try std.testing.expect(globMatch("[ab].png", "a.png"));
    try std.testing.expect(!globMatch("[ab].png", "c.png"));
    try std.testing.expect(globMatch("[^c].png", "a.png"));
    try std.testing.expect(globMatch("*shot*.png", "my-shot-2.png"));
}

test "a url or file spec expands to itself" {
    const gpa = std.testing.allocator;
    const one = try expand(gpa, undefined, "https://example.com/a.png", .url);
    defer freeInputs(gpa, one);
    try std.testing.expectEqual(@as(usize, 1), one.len);
    try std.testing.expectEqualStrings("https://example.com/a.png", one[0]);
}

test "a foreign scheme is refused before anything opens it" {
    try std.testing.expectError(Error.ForeignScheme, expand(std.testing.allocator, undefined, "ftp://h/a.png", .file));
}

//! Where a `@save` writes. Bare, the result lands beside its source with a `-stencil`
//! suffix, which is what makes a whole-directory script safe to run in place.
const std = @import("std");

const confine = @import("../confine.zig");
const image = @import("../image.zig");
const net = @import("../net.zig");

pub const Error = error{ SaveOutsideCwd, SaveTraversal };

pub const SUFFIX = "-stencil";

const Split = struct { dir: []const u8, base: []const u8 };

/// A source split at its last separator, any `?query`/`#fragment` trimmed first. `dir` keeps its trailing
/// slash and is EMPTY for a URL: a bare `@save` writes into the working directory, never at the host.
fn splitPath(path: []const u8) Split {
    var end = path.len;
    if (std.mem.indexOfAny(u8, path, "?#")) |q| end = q;
    const p = path[0..end];
    const slash = std.mem.lastIndexOfAny(u8, p, "/\\") orelse return .{ .dir = "", .base = p };
    return .{ .dir = if (net.isUrl(path)) "" else p[0 .. slash + 1], .base = p[slash + 1 ..] };
}

fn stem(name: []const u8) []const u8 {
    const dot = std.mem.lastIndexOfScalar(u8, name, '.');
    return if (dot) |i| (if (i == 0) name else name[0..i]) else name;
}

/// The frame a canvas is showing, or null when nothing named one: 0 is "as the source opens",
/// so it never reaches a `-frame-0` name. One rule for the runner and for the planner.
pub fn frameOf(frame: u32) ?u32 {
    return if (frame > 0) frame else null;
}

/// The path a `@save <target>` writes: "" → <source dir>/<stem>-stencil.<ext>, "dir/" → inside dir,
/// "name" → name.<ext> (no suffix), a full path verbatim. `frame` names a video grab <stem>-frame-<n>.
pub fn resolveTarget(
    gpa: std.mem.Allocator,
    target: []const u8,
    source: []const u8,
    frame: ?u32,
    fmt: image.Format,
) ![]u8 {
    const ext = fmt.ext();
    const src = splitPath(source);
    const src_stem = if (src.base.len == 0) "image" else stem(src.base);

    var stem_buf: std.ArrayList(u8) = .empty;
    defer stem_buf.deinit(gpa);
    try stem_buf.appendSlice(gpa, src_stem);
    if (frame) |n| {
        var fb: [32]u8 = undefined;
        try stem_buf.appendSlice(gpa, try std.fmt.bufPrint(&fb, "-frame-{d}", .{n}));
    }

    if (target.len == 0)
        return std.fmt.allocPrint(gpa, "{s}{s}{s}.{s}", .{ src.dir, stem_buf.items, SUFFIX, ext });

    if (target[target.len - 1] == '/' or target[target.len - 1] == '\\')
        return std.fmt.allocPrint(gpa, "{s}{s}{s}.{s}", .{ target, stem_buf.items, SUFFIX, ext });

    if (image.formatFromExt(image.extOf(target) orelse "") != null) return gpa.dupe(u8, target);
    return std.fmt.allocPrint(gpa, "{s}.{s}", .{ target, ext });
}

/// The same guard the one-shot pipeline applies: `..` is always refused, and under
/// --confine-output so is anything outside the working directory.
pub fn guard(path: []const u8, confine_output: bool) Error!void {
    if (confine.hasParentTraversal(path)) return Error.SaveTraversal;
    if (confine_output and confine.outsideCwd(path)) return Error.SaveOutsideCwd;
}

test "a bare save lands beside its source with the suffix" {
    const gpa = std.testing.allocator;
    const p = try resolveTarget(gpa, "", "shots/a.png", null, .png);
    defer gpa.free(p);
    try std.testing.expectEqualStrings("shots/a-stencil.png", p);
}

test "a directory target keeps the suffix, a named target does not" {
    const gpa = std.testing.allocator;
    const d = try resolveTarget(gpa, "out/", "shots/a.png", null, .png);
    defer gpa.free(d);
    try std.testing.expectEqualStrings("out/a-stencil.png", d);

    const n = try resolveTarget(gpa, "final", "shots/a.png", null, .jpeg);
    defer gpa.free(n);
    try std.testing.expectEqualStrings("final.jpg", n);
}

test "a target with a known extension is taken verbatim" {
    const gpa = std.testing.allocator;
    const p = try resolveTarget(gpa, "out/exact.jpg", "a.png", null, .png);
    defer gpa.free(p);
    try std.testing.expectEqualStrings("out/exact.jpg", p);
}

test "a video frame names itself" {
    const gpa = std.testing.allocator;
    const p = try resolveTarget(gpa, "", "clip.mp4", 90, .png);
    defer gpa.free(p);
    try std.testing.expectEqualStrings("clip-frame-90-stencil.png", p);
}

test "a url source still yields a sane local name" {
    const gpa = std.testing.allocator;
    const p = try resolveTarget(gpa, "out/", "https://example.com/pics/a.png?x=1", null, .png);
    defer gpa.free(p);
    try std.testing.expectEqualStrings("out/a-stencil.png", p);
}

test "a bare save on a url writes into the working directory, not back at the host" {
    const gpa = std.testing.allocator;
    const p = try resolveTarget(gpa, "", "https://example.com/pics/a.png?x=1", null, .png);
    defer gpa.free(p);
    try std.testing.expectEqualStrings("a-stencil.png", p);
}

test "frameOf treats 0 as no frame at all" {
    try std.testing.expectEqual(@as(?u32, null), frameOf(0));
    try std.testing.expectEqual(@as(?u32, 7), frameOf(7));
}

test "traversal is refused always, and outside-cwd under confinement" {
    try std.testing.expectError(Error.SaveTraversal, guard("../x.png", false));
    try std.testing.expectError(Error.SaveOutsideCwd, guard("/tmp/x.png", true));
    try guard("/tmp/x.png", false);
    try guard("out/x.png", true);
}

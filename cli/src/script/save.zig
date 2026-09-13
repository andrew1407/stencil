//! Where a `@save` writes. Bare, the result lands beside its source with a `-stencil`
//! suffix, which is what makes a whole-directory script safe to run in place.
const std = @import("std");

const confine = @import("../confine.zig");
const image = @import("../image.zig");

pub const Error = error{ SaveOutsideCwd, SaveTraversal };

pub const SUFFIX = "-stencil";

fn baseName(path: []const u8) []const u8 {
    var end = path.len;
    if (std.mem.indexOfAny(u8, path, "?#")) |q| end = q;
    const p = path[0..end];
    const slash = std.mem.lastIndexOfAny(u8, p, "/\\");
    return if (slash) |i| p[i + 1 ..] else p;
}

fn stem(name: []const u8) []const u8 {
    const dot = std.mem.lastIndexOfScalar(u8, name, '.');
    return if (dot) |i| (if (i == 0) name else name[0..i]) else name;
}

fn dirOf(path: []const u8) []const u8 {
    var end = path.len;
    if (std.mem.indexOfAny(u8, path, "?#")) |q| end = q;
    const p = path[0..end];
    const slash = std.mem.lastIndexOfAny(u8, p, "/\\");
    return if (slash) |i| p[0 .. i + 1] else "";
}

fn hasKnownExt(name: []const u8) bool {
    const dot = std.mem.lastIndexOfScalar(u8, name, '.') orelse return false;
    return image.formatFromExt(name[dot + 1 ..]) != null;
}

/// The path a `@save <target>` writes, for a result derived from `source`.
/// - ""            -> <source dir>/<stem>-stencil.<ext>
/// - "dir/"        -> dir/<stem>-stencil.<ext>
/// - "name"        -> name.<ext>            (the user named it; no suffix)
/// - "path/x.png"  -> verbatim
/// `frame` is non-null for a video grab, which names itself <stem>-frame-<n>.
pub fn resolveTarget(
    gpa: std.mem.Allocator,
    target: []const u8,
    source: []const u8,
    frame: ?u32,
    fmt: image.Format,
) ![]u8 {
    const ext = fmt.ext();
    const src_base = baseName(source);
    const src_stem = if (src_base.len == 0) "image" else stem(src_base);

    var stem_buf: std.ArrayList(u8) = .empty;
    defer stem_buf.deinit(gpa);
    try stem_buf.appendSlice(gpa, src_stem);
    if (frame) |n| {
        var fb: [32]u8 = undefined;
        try stem_buf.appendSlice(gpa, try std.fmt.bufPrint(&fb, "-frame-{d}", .{n}));
    }

    if (target.len == 0)
        return std.fmt.allocPrint(gpa, "{s}{s}{s}.{s}", .{ dirOf(source), stem_buf.items, SUFFIX, ext });

    if (target[target.len - 1] == '/' or target[target.len - 1] == '\\')
        return std.fmt.allocPrint(gpa, "{s}{s}{s}.{s}", .{ target, stem_buf.items, SUFFIX, ext });

    if (hasKnownExt(baseName(target))) return gpa.dupe(u8, target);
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

test "traversal is refused always, and outside-cwd under confinement" {
    try std.testing.expectError(Error.SaveTraversal, guard("../x.png", false));
    try std.testing.expectError(Error.SaveOutsideCwd, guard("/tmp/x.png", true));
    try guard("/tmp/x.png", false);
    try guard("out/x.png", true);
}

//! Output-path guards shared by the one-shot pipeline and the scraper. `..` traversal is refused
//! always; `--confine-output` adds the rest (an absolute path, a `~` home reference, a symbolic link
//! that leads out) for the mcp and bot adapters, which forward LLM-chosen paths. OFF by default.
const std = @import("std");

/// True when `path` has a ".." component (on either separator) that could climb
/// above the working directory.
pub fn hasParentTraversal(path: []const u8) bool {
    var it = std.mem.splitAny(u8, path, "/\\");
    while (it.next()) |seg| {
        if (std.mem.eql(u8, seg, "..")) return true;
    }
    return false;
}

/// True when `path` names a destination outside the working directory that only
/// `--confine-output` refuses: an absolute path, or a `~` the pipeline would expand to one.
pub fn outsideCwd(path: []const u8) bool {
    if (path.len == 0) return false;
    if (path[0] == '~') return true;
    return isAbsolute(path);
}

/// Everything `--confine-output` refuses: `path` outside `dir` by its spelling, or by where a
/// symbolic link on it leads.
pub fn escapes(io: std.Io, dir: std.Io.Dir, path: []const u8) bool {
    return outsideCwd(path) or escapesThroughLink(io, dir, path);
}

/// True when `path`, or else its deepest existing ancestor, resolves outside `dir`; a dangling
/// link or an unresolvable path counts as outside.
pub fn escapesThroughLink(io: std.Io, dir: std.Io.Dir, path: []const u8) bool {
    var root_buf: [std.fs.max_path_bytes]u8 = undefined;
    const root = root_buf[0 .. dir.realPathFile(io, ".", &root_buf) catch return true];
    var buf: [std.fs.max_path_bytes]u8 = undefined;
    var probe = path;
    while (true) {
        const sub = if (probe.len == 0) "." else probe;
        if (dir.realPathFile(io, sub, &buf)) |n| return !within(root, buf[0..n]) else |e| switch (e) {
            error.FileNotFound => {},
            else => return true,
        }
        const st = dir.statFile(io, sub, .{ .follow_symlinks = false }) catch null;
        if (st != null and st.?.kind == .sym_link) return true;
        if (probe.len == 0) return true;
        probe = std.fs.path.dirname(probe) orelse "";
    }
}

fn within(root: []const u8, p: []const u8) bool {
    if (!std.mem.startsWith(u8, p, root)) return false;
    if (p.len == root.len or std.fs.path.isSep(root[root.len - 1])) return true;
    return std.fs.path.isSep(p[root.len]);
}

/// Absolute on either platform: POSIX `/x`, a Windows drive (`C:\x`) or a UNC share.
fn isAbsolute(path: []const u8) bool {
    if (path[0] == '/' or path[0] == '\\') return true;
    // A drive prefix counts with or without the separator: `C:out.png` is drive-RELATIVE, so
    // Windows resolves it against that drive's own cwd, not ours.
    return path.len >= 2 and std.ascii.isAlphabetic(path[0]) and path[1] == ':';
}

const testing = std.testing;

test "hasParentTraversal spots a climbing segment on either separator" {
    try testing.expect(hasParentTraversal("../out.png"));
    try testing.expect(hasParentTraversal("sub/../../out.png"));
    try testing.expect(hasParentTraversal("sub\\..\\out.png"));
    try testing.expect(!hasParentTraversal("out.png"));
    try testing.expect(!hasParentTraversal("sub/out.png"));
    try testing.expect(!hasParentTraversal("..hidden/out.png")); // not a ".." SEGMENT
}

test "outsideCwd: only what --confine-output adds (absolute paths and ~)" {
    try testing.expect(outsideCwd("/etc/evil.png"));
    try testing.expect(outsideCwd("/tmp/out.png"));
    try testing.expect(outsideCwd("~/Downloads/out.png"));
    try testing.expect(outsideCwd("~"));
    try testing.expect(outsideCwd("C:\\Windows\\out.png"));
    try testing.expect(outsideCwd("\\\\server\\share\\out.png"));
    // Relative destinations stay allowed — confinement never blocks the working directory.
    try testing.expect(!outsideCwd("out.png"));
    try testing.expect(!outsideCwd("sub/dir/out.png"));
    try testing.expect(!outsideCwd("./out.png"));
    try testing.expect(!outsideCwd(""));
}

test "escapesThroughLink: a link inside the directory that leads out, or dangles, is refused" {
    if (@import("builtin").os.tag == .windows) return error.SkipZigTest; // links need privileges there
    const io = testing.io;
    var root = testing.tmpDir(.{});
    defer root.cleanup();
    var away = testing.tmpDir(.{});
    defer away.cleanup();
    var away_buf: [std.fs.max_path_bytes]u8 = undefined;
    const away_path = away_buf[0..try away.dir.realPath(io, &away_buf)];
    try root.dir.createDirPath(io, "sub");
    try root.dir.symLink(io, away_path, "out", .{ .is_directory = true });
    try root.dir.symLink(io, "sub", "inner", .{ .is_directory = true });
    try root.dir.symLink(io, "missing/target.png", "dangling.png", .{});
    try testing.expect(escapesThroughLink(io, root.dir, "out/x.png"));
    try testing.expect(escapesThroughLink(io, root.dir, "out/new/dir/x.png"));
    try testing.expect(escapesThroughLink(io, root.dir, "dangling.png"));
    try testing.expect(!escapesThroughLink(io, root.dir, "inner/x.png")); // a link that stays inside
    try testing.expect(!escapesThroughLink(io, root.dir, "sub/new/x.png"));
    try testing.expect(!escapesThroughLink(io, root.dir, "x.png"));
    try testing.expect(escapes(io, root.dir, "/tmp/x.png") and !escapes(io, root.dir, "sub/x.png"));
}

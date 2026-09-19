//! Output-path guards shared by the one-shot pipeline and the scraper. `..` traversal is refused
//! always; `--confine-output` adds the rest (an absolute path, a `~` home reference) for the mcp
//! and bot adapters, which forward LLM-chosen paths. OFF by default: a human writes where they name.
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

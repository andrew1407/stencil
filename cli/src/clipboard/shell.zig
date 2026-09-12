//! What both directions need to shell out: the error set, the size cap, the scratch PNG
//! path in the per-user temp dir, and running a tool with "not installed" told apart from
//! a real failure.
const std = @import("std");
const builtin = @import("builtin");
const child = @import("../child.zig");

pub const Error = error{ Unsupported, ToolMissing, NoImage, Failed };

pub const MAX_IMAGE = 64 << 20; // 64 MiB cap on a clipboard image

// The per-user temp directory ($TMPDIR, as macOS sets it), any trailing slash trimmed; falls
// back to /tmp. Preferred over a hardcoded /tmp so the scratch file isn't a predictable name in
// a world-writable dir. Scratch filenames also carry the PID, so concurrent CLI instances never
// collide on the same temp file.
pub fn tmpDir() []const u8 {
    if (builtin.os.tag == .windows) {
        const t = std.c.getenv("TEMP") orelse std.c.getenv("TMP") orelse return ".";
        return std.mem.trimEnd(u8, std.mem.span(t), "\\/");
    }
    const t = std.c.getenv("TMPDIR") orelse return "/tmp";
    return std.mem.trimEnd(u8, std.mem.span(t), "/");
}

// A scratch PNG path of our own, in the per-user temp dir and carrying the PID.
pub fn scratchPath(gpa: std.mem.Allocator, comptime name: []const u8) ![]u8 {
    const sep: []const u8 = if (builtin.os.tag == .windows) "\\" else "/";
    return std.fmt.allocPrint(gpa, "{s}{s}stencil_clip_{s}.{d}.png", .{ tmpDir(), sep, name, std.c.getpid() });
}

/// Read an image off the clipboard as owned PNG bytes (caller frees). `NoImage` when the
/// clipboard holds no picture (and no picture FILE) — the ordinary "nothing to paste" case.

// Run a tool, mapping "not installed" to ToolMissing and a non-zero exit to Failed.
pub fn runOrFail(gpa: std.mem.Allocator, io: std.Io, argv: []const []const u8) !void {
    const res = child.run(gpa, io, .{ .argv = argv }) catch |e| switch (e) {
        error.FileNotFound => return Error.ToolMissing,
        else => return e,
    };
    defer gpa.free(res.stderr);
    defer gpa.free(res.stdout);
    if (!exitedOk(res.term)) return Error.Failed;
}

/// `s` with backslashes and single quotes escaped, so it can sit inside a '…' JS literal.
pub fn escapeJs(gpa: std.mem.Allocator, s: []const u8) ![]u8 {
    var out: std.ArrayList(u8) = .empty;
    errdefer out.deinit(gpa);
    for (s) |c| {
        if (c == '\\' or c == '\'') try out.append(gpa, '\\');
        try out.append(gpa, c);
    }
    return out.toOwnedSlice(gpa);
}

pub fn exitedOk(term: std.process.Child.Term) bool {
    return switch (term) {
        .exited => |code| code == 0,
        else => false,
    };
}

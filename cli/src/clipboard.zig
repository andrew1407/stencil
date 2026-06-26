//! Clipboard image I/O for the console's `/paste` (clipboard → working image) and `/copy`
//! (encoded result → clipboard). Like video.zig this shells out rather than pulling a
//! platform GUI dependency into the codec-free pipeline. macOS is supported via `osascript`
//! (the clipboard's «class PNGf» flavour); other platforms return `Unsupported` with a clear
//! message at the call site. PNG is the interchange format both directions.
const std = @import("std");
const builtin = @import("builtin");

pub const Error = error{ Unsupported, ToolMissing, NoImage, Failed };

const MAX_IMAGE = 64 << 20; // 64 MiB cap on a clipboard image

// AppleScript: dump the clipboard's PNG to a temp file and echo its path, or "" if the
// clipboard holds no image.
const read_script =
    \\set p to (POSIX path of (path to temporary items)) & "stencil_clip_in.png"
    \\try
    \\  set d to the clipboard as «class PNGf»
    \\on error
    \\  return ""
    \\end try
    \\set f to open for access (POSIX file p) with write permission
    \\set eof f to 0
    \\write d to f
    \\close access f
    \\return p
;

/// Read a PNG image off the clipboard into owned bytes (caller frees).
pub fn readImage(gpa: std.mem.Allocator, io: std.Io) ![]u8 {
    if (builtin.os.tag != .macos) return Error.Unsupported;

    const res = std.process.run(gpa, io, .{ .argv = &.{ "osascript", "-e", read_script } }) catch |e| switch (e) {
        error.FileNotFound => return Error.ToolMissing,
        else => return e,
    };
    defer gpa.free(res.stderr);
    defer gpa.free(res.stdout);
    if (!exitedOk(res.term)) return Error.Failed;

    const path = std.mem.trim(u8, res.stdout, " \t\r\n");
    if (path.len == 0) return Error.NoImage;

    const dir = std.Io.Dir.cwd();
    const bytes = dir.readFileAlloc(io, path, gpa, .limited(MAX_IMAGE)) catch return Error.Failed;
    dir.deleteFile(io, path) catch {};
    return bytes;
}

/// Put PNG bytes onto the clipboard.
pub fn writeImage(gpa: std.mem.Allocator, io: std.Io, png: []const u8) !void {
    if (builtin.os.tag != .macos) return Error.Unsupported;

    const path = "/tmp/stencil_clip_out.png";
    const dir = std.Io.Dir.cwd();
    dir.writeFile(io, .{ .sub_path = path, .data = png }) catch return Error.Failed;
    defer dir.deleteFile(io, path) catch {};

    const script = try std.fmt.allocPrint(gpa, "set the clipboard to (read (POSIX file \"{s}\") as «class PNGf»)", .{path});
    defer gpa.free(script);

    const res = std.process.run(gpa, io, .{ .argv = &.{ "osascript", "-e", script } }) catch |e| switch (e) {
        error.FileNotFound => return Error.ToolMissing,
        else => return e,
    };
    defer gpa.free(res.stderr);
    defer gpa.free(res.stdout);
    if (!exitedOk(res.term)) return Error.Failed;
}

fn exitedOk(term: std.process.Child.Term) bool {
    return switch (term) {
        .exited => |code| code == 0,
        else => false,
    };
}

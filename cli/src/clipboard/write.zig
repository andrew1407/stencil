//! Image (and text) → clipboard: PNG is the interchange format, written through the same
//! per-platform tools the reader drives.
const std = @import("std");
const builtin = @import("builtin");
const shell = @import("shell.zig");

const Error = shell.Error;
const scratchPath = shell.scratchPath;
const tmpDir = shell.tmpDir;
const runOrFail = shell.runOrFail;

pub fn writeImage(gpa: std.mem.Allocator, io: std.Io, png: []const u8) !void {
    if (builtin.os.tag != .macos and builtin.os.tag != .linux and builtin.os.tag != .windows) return Error.Unsupported;

    const path = try scratchPath(gpa, "out");
    defer gpa.free(path);
    const dir = std.Io.Dir.cwd();
    defer dir.deleteFile(io, path) catch {}; // before the write: a partial one is removed too
    dir.writeFile(io, .{ .sub_path = path, .data = png }) catch return Error.Failed;

    switch (builtin.os.tag) {
        .macos => {
            const script = try gpa.print("set the clipboard to (read (POSIX file \"{s}\") as «class PNGf»)", .{path});
            defer gpa.free(script);
            try runOrFail(gpa, io, &.{ "osascript", "-e", script });
        },
        // wl-copy takes the bytes on stdin, xclip reads the file directly; try Wayland first.
        .linux => {
            const cmd = try shell.detachedLine(gpa, "wl-copy --type image/png < '{s}' || xclip -selection clipboard -t image/png -i '{s}'", .{ path, path });
            defer gpa.free(cmd);
            try runOrFail(gpa, io, &.{ "sh", "-c", cmd });
        },
        .windows => {
            const ps = try gpa.print(
                \\Add-Type -AssemblyName System.Windows.Forms, System.Drawing;
                \\$i = [System.Drawing.Image]::FromFile('{s}');
                \\[Windows.Forms.Clipboard]::SetImage($i)
            , .{path});
            defer gpa.free(ps);
            try runOrFail(gpa, io, &.{ "powershell", "-NoProfile", "-STA", "-Command", ps });
        },
        else => unreachable,
    }
}

/// Put UTF-8 `text` on the clipboard. Used by the full-screen console's Ctrl-S "copy the
/// selection". Routed through a temp file to avoid stdin plumbing, mirroring writeImage.
pub fn writeText(gpa: std.mem.Allocator, io: std.Io, text: []const u8) !void {
    if (builtin.os.tag != .macos and builtin.os.tag != .linux and builtin.os.tag != .windows) return Error.Unsupported;

    const sep: []const u8 = if (builtin.os.tag == .windows) "\\" else "/";
    const path = try gpa.print("{s}{s}stencil_clip_text.{d}.txt", .{ tmpDir(), sep, std.c.getpid() });
    defer gpa.free(path);
    const dir = std.Io.Dir.cwd();
    defer dir.deleteFile(io, path) catch {};
    dir.writeFile(io, .{ .sub_path = path, .data = text }) catch return Error.Failed;

    switch (builtin.os.tag) {
        .macos => {
            const cmd = try gpa.print("pbcopy < '{s}'", .{path});
            defer gpa.free(cmd);
            try runOrFail(gpa, io, &.{ "sh", "-c", cmd });
        },
        // Each tool reads the file itself: a bare `wl-copy` would read the console's own stdin.
        .linux => {
            const cmd = try shell.detachedLine(gpa, "wl-copy < '{s}' || xclip -selection clipboard -i '{s}'", .{ path, path });
            defer gpa.free(cmd);
            try runOrFail(gpa, io, &.{ "sh", "-c", cmd });
        },
        .windows => {
            const ps = try gpa.print("Get-Content -Raw -LiteralPath '{s}' | Set-Clipboard", .{path});
            defer gpa.free(ps);
            try runOrFail(gpa, io, &.{ "powershell", "-NoProfile", "-Command", ps });
        },
        else => unreachable,
    }
}

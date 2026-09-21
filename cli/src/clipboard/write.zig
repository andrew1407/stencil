//! Image (and text) → clipboard: PNG is the interchange format, written through the same
//! per-platform tools the reader drives.
const std = @import("std");
const builtin = @import("builtin");
const child = @import("../safety/child.zig");
const shell = @import("shell.zig");

const Error = shell.Error;
const scratchPath = shell.scratchPath;
const tmpDir = shell.tmpDir;
const escapeJs = shell.escapeJs;
const runOrFail = shell.runOrFail;

pub fn writeImage(gpa: std.mem.Allocator, io: std.Io, png: []const u8) !void {
    if (builtin.os.tag != .macos and builtin.os.tag != .linux and builtin.os.tag != .windows) return Error.Unsupported;

    const path = try scratchPath(gpa, "out");
    defer gpa.free(path);
    const dir = std.Io.Dir.cwd();
    dir.writeFile(io, .{ .sub_path = path, .data = png }) catch return Error.Failed;
    defer dir.deleteFile(io, path) catch {};

    switch (builtin.os.tag) {
        .macos => {
            const script = try std.fmt.allocPrint(gpa, "set the clipboard to (read (POSIX file \"{s}\") as «class PNGf»)", .{path});
            defer gpa.free(script);
            try runOrFail(gpa, io, &.{ "osascript", "-e", script });
        },
        // wl-copy takes the bytes on stdin, xclip reads the file directly; try Wayland first.
        .linux => {
            const cmd = try std.fmt.allocPrint(gpa, "wl-copy --type image/png < '{s}' || xclip -selection clipboard -t image/png -i '{s}'", .{ path, path });
            defer gpa.free(cmd);
            try runOrFail(gpa, io, &.{ "sh", "-c", cmd });
        },
        .windows => {
            const ps = try std.fmt.allocPrint(gpa,
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
    const path = try std.fmt.allocPrint(gpa, "{s}{s}stencil_clip_text.{d}.txt", .{ tmpDir(), sep, std.c.getpid() });
    defer gpa.free(path);
    const dir = std.Io.Dir.cwd();
    dir.writeFile(io, .{ .sub_path = path, .data = text }) catch return Error.Failed;
    defer dir.deleteFile(io, path) catch {};

    switch (builtin.os.tag) {
        .macos, .linux => {
            const tool: []const u8 = if (builtin.os.tag == .macos) "pbcopy" else "wl-copy || xclip -selection clipboard -i";
            const cmd = try std.fmt.allocPrint(gpa, "{s} < '{s}'", .{ tool, path });
            defer gpa.free(cmd);
            try runOrFail(gpa, io, &.{ "sh", "-c", cmd });
        },
        .windows => {
            const ps = try std.fmt.allocPrint(gpa, "Get-Content -Raw -LiteralPath '{s}' | Set-Clipboard", .{path});
            defer gpa.free(ps);
            try runOrFail(gpa, io, &.{ "powershell", "-NoProfile", "-Command", ps });
        },
        else => unreachable,
    }
}

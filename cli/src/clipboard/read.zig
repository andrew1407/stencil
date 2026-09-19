//! Clipboard → image (and text). Takes what is actually there, not one blessed flavour: a
//! PNG, else a TIFF re-encoded to PNG (what most macOS apps and many Linux ones put on the
//! board), else an image FILE copied in a file manager.
const std = @import("std");
const builtin = @import("builtin");
const child = @import("../child.zig");
const shell = @import("shell.zig");

const Error = shell.Error;
const MAX_IMAGE = shell.MAX_IMAGE;
const tmpDir = shell.tmpDir;
const scratchPath = shell.scratchPath;
const escapeJs = shell.escapeJs;
const exitedOk = shell.exitedOk;
const runOrFail = shell.runOrFail;

pub fn readImage(gpa: std.mem.Allocator, io: std.Io) ![]u8 {
    return switch (builtin.os.tag) {
        .macos => readMac(gpa, io),
        .linux => readLinux(gpa, io),
        .windows => readWindows(gpa, io),
        else => Error.Unsupported,
    };
}

// macOS: NSPasteboard through JXA (`osascript -l JavaScript`). The script writes a PNG to OUT and
// echoes "ok"; its sources, in order, are board PNG, board TIFF (4 = NSPNGFileType), file URL.
const mac_js_head =
    \\ObjC.import('AppKit');
    \\var OUT = '
;
const mac_js_tail =
    \\';
    \\function write(data) {
    \\  if (!data || data.isNil() || data.length === 0) return false;
    \\  return data.writeToFileAtomically($(OUT), true);
    \\}
    \\function pngFrom(tiff) {
    \\  if (!tiff || tiff.isNil() || tiff.length === 0) return false;
    \\  var rep = $.NSBitmapImageRep.imageRepWithData(tiff);
    \\  if (rep.isNil()) return false;
    \\  return write(rep.representationUsingTypeProperties(4, $.NSDictionary.dictionary));
    \\}
    \\function run() {
    \\  var pb = $.NSPasteboard.generalPasteboard;
    \\  if (write(pb.dataForType($.NSPasteboardTypePNG))) return 'ok';
    \\  if (pngFrom(pb.dataForType($.NSPasteboardTypeTIFF))) return 'ok';
    \\  var s = pb.stringForType($('public.file-url'));
    \\  if (!s.isNil() && s.js.length > 0) {
    \\    var img = $.NSImage.alloc.initWithContentsOfURL($.NSURL.URLWithString(s));
    \\    if (!img.isNil() && pngFrom(img.TIFFRepresentation)) return 'ok';
    \\  }
    \\  return '';
    \\}
;

fn readMac(gpa: std.mem.Allocator, io: std.Io) ![]u8 {
    const path = try scratchPath(gpa, "in");
    defer gpa.free(path);
    const dir = std.Io.Dir.cwd();
    dir.deleteFile(io, path) catch {}; // never hand back a stale read
    defer dir.deleteFile(io, path) catch {};

    // The path lands inside a JS string literal, so escape what would end it.
    const quoted = try escapeJs(gpa, path);
    defer gpa.free(quoted);
    const script = try std.mem.concat(gpa, u8, &.{ mac_js_head, quoted, mac_js_tail });
    defer gpa.free(script);

    const res = child.run(gpa, io, .{ .argv = &.{ "osascript", "-l", "JavaScript", "-e", script } }) catch |e| switch (e) {
        error.FileNotFound => return Error.ToolMissing,
        else => return e,
    };
    defer gpa.free(res.stderr);
    defer gpa.free(res.stdout);
    if (!exitedOk(res.term)) return Error.Failed;
    if (std.mem.indexOf(u8, res.stdout, "ok") == null) return Error.NoImage;
    return dir.readFileAlloc(io, path, gpa, .limited(MAX_IMAGE)) catch return Error.NoImage;
}

// Linux: Wayland's wl-paste first, then X11's xclip. Both write the selection's image/png
// bytes to stdout; an empty result (or a board with no image/png target) is NoImage.
fn readLinux(gpa: std.mem.Allocator, io: std.Io) ![]u8 {
    var missing: usize = 0;
    for ([_][]const []const u8{
        &.{ "wl-paste", "--no-newline", "--type", "image/png" },
        &.{ "xclip", "-selection", "clipboard", "-t", "image/png", "-o" },
    }) |argv| {
        const res = child.run(gpa, io, .{ .argv = argv, .stdout_limit = .limited(MAX_IMAGE) }) catch |e| switch (e) {
            error.FileNotFound => {
                missing += 1;
                continue;
            },
            else => return e,
        };
        defer gpa.free(res.stderr);
        errdefer gpa.free(res.stdout);
        if (exitedOk(res.term) and res.stdout.len != 0) return res.stdout;
        gpa.free(res.stdout);
    }
    return if (missing == 2) Error.ToolMissing else Error.NoImage;
}

// Windows: PowerShell reads the clipboard's image (or the first image FILE copied in
// Explorer) and saves it as a PNG we then read back.
const win_ps =
    \\Add-Type -AssemblyName System.Windows.Forms, System.Drawing;
    \\$out = $args[0];
    \\$img = [Windows.Forms.Clipboard]::GetImage();
    \\if ($img -ne $null) { $img.Save($out, [System.Drawing.Imaging.ImageFormat]::Png); exit 0 }
    \\$files = [Windows.Forms.Clipboard]::GetFileDropList();
    \\foreach ($f in $files) {
    \\  try { $i = [System.Drawing.Image]::FromFile($f); $i.Save($out, [System.Drawing.Imaging.ImageFormat]::Png); exit 0 } catch { }
    \\}
    \\exit 1
;

fn readWindows(gpa: std.mem.Allocator, io: std.Io) ![]u8 {
    const path = try scratchPath(gpa, "in");
    defer gpa.free(path);
    const dir = std.Io.Dir.cwd();
    dir.deleteFile(io, path) catch {};
    defer dir.deleteFile(io, path) catch {};

    const res = child.run(gpa, io, .{ .argv = &.{ "powershell", "-NoProfile", "-STA", "-Command", win_ps, "-args", path } }) catch |e| switch (e) {
        error.FileNotFound => return Error.ToolMissing,
        else => return e,
    };
    defer gpa.free(res.stderr);
    defer gpa.free(res.stdout);
    if (!exitedOk(res.term)) return Error.NoImage; // exit 1 = nothing on the board to take
    return dir.readFileAlloc(io, path, gpa, .limited(MAX_IMAGE)) catch return Error.NoImage;
}

/// Read the clipboard's TEXT as owned bytes — what Ctrl-V falls back to when the board holds no
/// picture. Empty (or no tool) reads as `NoImage`, the "nothing to take" case.
pub fn readText(gpa: std.mem.Allocator, io: std.Io) ![]u8 {
    const argv: []const []const u8 = switch (builtin.os.tag) {
        .macos => &.{"pbpaste"},
        .linux => &.{ "wl-paste", "--no-newline" },
        .windows => &.{ "powershell", "-NoProfile", "-Command", "Get-Clipboard -Raw" },
        else => return Error.Unsupported,
    };
    const res = child.run(gpa, io, .{ .argv = argv, .stdout_limit = .limited(1 << 20) }) catch |e| switch (e) {
        // X11 without wl-paste: xclip is the other half of the Linux pair.
        error.FileNotFound => if (builtin.os.tag == .linux) return readTextXclip(gpa, io) else return Error.ToolMissing,
        else => return e,
    };
    defer gpa.free(res.stderr);
    errdefer gpa.free(res.stdout);
    if (!exitedOk(res.term) or res.stdout.len == 0) {
        gpa.free(res.stdout);
        return Error.NoImage;
    }
    return res.stdout;
}

fn readTextXclip(gpa: std.mem.Allocator, io: std.Io) ![]u8 {
    const res = child.run(gpa, io, .{ .argv = &.{ "xclip", "-selection", "clipboard", "-o" }, .stdout_limit = .limited(1 << 20) }) catch |e| switch (e) {
        error.FileNotFound => return Error.ToolMissing,
        else => return e,
    };
    defer gpa.free(res.stderr);
    errdefer gpa.free(res.stdout);
    if (!exitedOk(res.term) or res.stdout.len == 0) {
        gpa.free(res.stdout);
        return Error.NoImage;
    }
    return res.stdout;
}

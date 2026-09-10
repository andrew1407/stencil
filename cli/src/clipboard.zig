//! Clipboard image I/O for the console's `/paste` (clipboard → working image) and `/copy`
//! (encoded result → clipboard). Like video.zig this shells out rather than pulling a
//! platform GUI dependency into the codec-free pipeline; PNG is the interchange format both
//! directions. Supported: **macOS** (`osascript`, driving NSPasteboard through JavaScript for
//! Automation), **Linux** (`wl-paste`/`wl-copy` on Wayland, else `xclip`) and **Windows**
//! (PowerShell). Anything else returns `Unsupported` with a clear message at the call site.
//!
//! Reading takes what is actually there, not one blessed flavour: a PNG, else a TIFF (what most
//! macOS apps and many Linux ones put on the board) re-encoded to PNG, else an image FILE copied
//! in a file manager. AppleScript's `the clipboard as «class PNGf»` — what this used to use —
//! fails with -1700 on a rich multi-flavour clipboard even while `clipboard info` lists PNGf.
const std = @import("std");
const builtin = @import("builtin");
const child = @import("child.zig");

pub const Error = error{ Unsupported, ToolMissing, NoImage, Failed };

const MAX_IMAGE = 64 << 20; // 64 MiB cap on a clipboard image

// The per-user temp directory ($TMPDIR, as macOS sets it), any trailing slash trimmed; falls
// back to /tmp. Preferred over a hardcoded /tmp so the scratch file isn't a predictable name in
// a world-writable dir. Scratch filenames also carry the PID, so concurrent CLI instances never
// collide on the same temp file.
fn tmpDir() []const u8 {
    if (builtin.os.tag == .windows) {
        const t = std.c.getenv("TEMP") orelse std.c.getenv("TMP") orelse return ".";
        return std.mem.trimEnd(u8, std.mem.span(t), "\\/");
    }
    const t = std.c.getenv("TMPDIR") orelse return "/tmp";
    return std.mem.trimEnd(u8, std.mem.span(t), "/");
}

// A scratch PNG path of our own, in the per-user temp dir and carrying the PID.
fn scratchPath(gpa: std.mem.Allocator, comptime name: []const u8) ![]u8 {
    const sep: []const u8 = if (builtin.os.tag == .windows) "\\" else "/";
    return std.fmt.allocPrint(gpa, "{s}{s}stencil_clip_{s}.{d}.png", .{ tmpDir(), sep, name, std.c.getpid() });
}

// ── reading (clipboard → PNG bytes) ───────────────────────────────────────────

/// Read an image off the clipboard as owned PNG bytes (caller frees). `NoImage` when the
/// clipboard holds no picture (and no picture FILE) — the ordinary "nothing to paste" case.
pub fn readImage(gpa: std.mem.Allocator, io: std.Io) ![]u8 {
    return switch (builtin.os.tag) {
        .macos => readMac(gpa, io),
        .linux => readLinux(gpa, io),
        .windows => readWindows(gpa, io),
        else => Error.Unsupported,
    };
}

// macOS: NSPasteboard through JXA (`osascript -l JavaScript`), which every macOS ships. The
// script writes a PNG to OUT and echoes "ok"; the three sources it tries, in order, are the
// board's PNG data, its TIFF data (re-encoded — 4 is NSPNGFileType), and an image file copied
// in Finder (public.file-url).
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

/// Read the clipboard's TEXT as owned bytes (caller frees) — what Ctrl-V falls back to when
/// the board holds no picture. Empty (or no tool) reads as `NoImage`, the "nothing to take"
/// case, so the caller can say one thing about an empty clipboard.
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

// ── writing (PNG bytes / text → clipboard) ────────────────────────────────────

/// Put PNG bytes onto the clipboard.
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

// ── plumbing ──────────────────────────────────────────────────────────────────

// Run a tool, mapping "not installed" to ToolMissing and a non-zero exit to Failed.
fn runOrFail(gpa: std.mem.Allocator, io: std.Io, argv: []const []const u8) !void {
    const res = child.run(gpa, io, .{ .argv = argv }) catch |e| switch (e) {
        error.FileNotFound => return Error.ToolMissing,
        else => return e,
    };
    defer gpa.free(res.stderr);
    defer gpa.free(res.stdout);
    if (!exitedOk(res.term)) return Error.Failed;
}

/// `s` with backslashes and single quotes escaped, so it can sit inside a '…' JS literal.
fn escapeJs(gpa: std.mem.Allocator, s: []const u8) ![]u8 {
    var out: std.ArrayList(u8) = .empty;
    errdefer out.deinit(gpa);
    for (s) |c| {
        if (c == '\\' or c == '\'') try out.append(gpa, '\\');
        try out.append(gpa, c);
    }
    return out.toOwnedSlice(gpa);
}

fn exitedOk(term: std.process.Child.Term) bool {
    return switch (term) {
        .exited => |code| code == 0,
        else => false,
    };
}

const testing = std.testing;

test "escapeJs: a path can never end the script's string literal" {
    const a = testing.allocator;
    const got = try escapeJs(a, "/tmp/o'brien\\x/stencil_clip_in.7.png");
    defer a.free(got);
    try testing.expectEqualStrings("/tmp/o\\'brien\\\\x/stencil_clip_in.7.png", got);
}

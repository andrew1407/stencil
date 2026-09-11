//! Clipboard image I/O for the console's `/paste` (clipboard → working image) and `/copy`
//! (encoded result → clipboard). Like video.zig this shells out rather than pulling a
//! platform GUI dependency into the codec-free pipeline; PNG is the interchange format both
//! directions. Supported: **macOS** (`osascript`, driving NSPasteboard through JavaScript for
//! Automation), **Linux** (`wl-paste`/`wl-copy` on Wayland, else `xclip`) and **Windows**
//! (PowerShell). Anything else returns `Unsupported` with a clear message at the call site.
//!
//! Reading takes what is actually there, not one blessed flavour: a PNG, else a TIFF (what most
//! macOS apps and many Linux ones put on the board) re-encoded to PNG, else an image FILE copied
//! in a file manager. AppleScript's `the clipboard as «class PNGf»` fails with -1700 on a rich
//! multi-flavour clipboard even while `clipboard info` lists PNGf.
const std = @import("std");
const builtin = @import("builtin");

const shell = @import("clipboard/shell.zig");
const read = @import("clipboard/read.zig");
const write = @import("clipboard/write.zig");

pub const Error = shell.Error;
const escapeJs = shell.escapeJs;

pub const readImage = read.readImage;
pub const readText = read.readText;
pub const writeImage = write.writeImage;
pub const writeText = write.writeText;

const testing = std.testing;

test "escapeJs: a path can never end the script's string literal" {
    const a = testing.allocator;
    const got = try escapeJs(a, "/tmp/o'brien\\x/stencil_clip_in.7.png");
    defer a.free(got);
    try testing.expectEqualStrings("/tmp/o\\'brien\\\\x/stencil_clip_in.7.png", got);
}

test {
    _ = shell;
    _ = read;
    _ = write;
}

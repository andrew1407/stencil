//! Clipboard image I/O for the console's `/paste` and `/copy`. Like video.zig it shells out rather
//! than pulling a platform GUI dependency into the codec-free pipeline; PNG is the interchange
//! format both directions. macOS (`osascript` JXA), Linux (`wl-paste`/`wl-copy`, else `xclip`) and
//! Windows (PowerShell); anything else returns `Unsupported`. A read takes what is there: a PNG,
//! else a TIFF re-encoded, else an image FILE copied in a file manager.
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

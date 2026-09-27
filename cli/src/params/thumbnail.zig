//! `--thumbnail <px>`: the one-shot pipeline shrinks its result so the longer side fits. The
//! flag's arm stays in parse.zig, where the --help drift test reads the flag spellings; this
//! file owns its value and the modes it may ride with.
const std = @import("std");
const logo = @import("../app/logo.zig");
const testing = std.testing;
const options = @import("options.zig");
const state = @import("state.zig");
const parser = @import("parse.zig");

const Error = options.Error;
const Options = options.Options;

/// The longer-side bound in pixels: a positive integer.
pub fn side(v: []const u8) Error!u32 {
    const px = try state.parseU32(v);
    if (px == 0) {
        logo.err("--thumbnail expects a size of at least 1 px, got '{s}'\n", .{v});
        return Error.BadValue;
    }
    return px;
}

/// Only the one-shot raster pipeline writes the image it shrinks; every other mode would drop it.
pub fn finish(opts: Options) Error!void {
    if (opts.thumbnail == null) return;
    switch (options.modeOf(opts, 1, isStencil)) {
        .usage, .pipeline => {},
        else => {
            logo.err("--thumbnail shrinks a one-shot image result; it does not ride with this mode\n", .{});
            return Error.BadValue;
        },
    }
}

// The project mode's own test (project/codec.zig isStencilPath), which params/ may not import.
fn isStencil(path: []const u8) bool {
    return std.ascii.endsWithIgnoreCase(path, ".stencil");
}

test "parse: --thumbnail takes a positive size, off unless given" {
    const on = [_][:0]const u8{ "-i", "in.png", "--thumbnail", "512", "out.png" };
    try testing.expectEqual(@as(?u32, 512), (try parser.parse(&on)).thumbnail);
    const off = [_][:0]const u8{ "-i", "in.png", "out.png" };
    try testing.expectEqual(@as(?u32, null), (try parser.parse(&off)).thumbnail);
    const zero = [_][:0]const u8{ "-i", "in.png", "--thumbnail", "0", "out.png" };
    try testing.expectError(Error.BadValue, parser.parse(&zero));
    const junk = [_][:0]const u8{ "-i", "in.png", "--thumbnail", "big", "out.png" };
    try testing.expectError(Error.BadNumber, parser.parse(&junk));
    const bare = [_][:0]const u8{ "-i", "in.png", "--thumbnail" };
    try testing.expectError(Error.MissingValue, parser.parse(&bare));
}

test "parse: --thumbnail rides only with the one-shot pipeline" {
    const server = [_][:0]const u8{ "--server", "http://h", "-i", "Plans", "--thumbnail", "64", "out.png" };
    try testing.expectEqual(@as(?u32, 64), (try parser.parse(&server)).thumbnail);
    const help = [_][:0]const u8{ "--help", "--thumbnail", "64" };
    try testing.expect((try parser.parse(&help)).help);
    const refused = [_][]const [:0]const u8{
        &.{ "--script", "s.stc", "--thumbnail", "64" },
        &.{ "--probe", "-i", "a.png", "--thumbnail", "64" },
        &.{ "--source-site", "http://h/", "--thumbnail", "64", "out/" },
        &.{ "--console", "--thumbnail", "64" },
        &.{ "-i", "a.png", "--thumbnail", "64", "out.stencil" },
        &.{ "-i", "a.STENCIL", "--thumbnail", "64", "out.png" },
    };
    for (refused) |argv| try testing.expectError(Error.BadValue, parser.parse(argv));
}

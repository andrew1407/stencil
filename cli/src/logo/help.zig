//! `--help`: the embedded prose and the two bold markers it carries.
const std = @import("std");
const logo = @import("../logo.zig");
const palette = @import("palette.zig");

const Ansi = palette.Ansi;
const print = logo.print;
const c = logo.colorSeq;

pub fn usage() void {
    emitMarked(help_text, c(Ansi.bold), c(Ansi.reset));
}

// The `--help` prose lives in the embedded help.txt, cross-checked against args.zig's flag table by
// tests/help_flags_test.zig. `{b}`/`{r}` are the only markers — bold on, bold off.
const help_text = @embedFile("../help.txt");

/// Print `text`, expanding the `{b}`/`{r}` markers; any other `{` is literal.
fn emitMarked(text: []const u8, bold: []const u8, reset: []const u8) void {
    var rest = text;
    while (std.mem.indexOfScalar(u8, rest, '{')) |at| {
        if (at != 0) print("{s}", .{rest[0..at]});
        const tail = rest[at..];
        if (std.mem.startsWith(u8, tail, "{b}")) {
            print("{s}", .{bold});
            rest = tail[3..];
        } else if (std.mem.startsWith(u8, tail, "{r}")) {
            print("{s}", .{reset});
            rest = tail[3..];
        } else {
            print("{{", .{});
            rest = tail[1..];
        }
    }
    if (rest.len != 0) print("{s}", .{rest});
}

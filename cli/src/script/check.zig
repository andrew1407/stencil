//! `--script-check <file>`: print every diagnostic and exit non-zero if any is an error.
//! Written for an editor to parse, so it goes to STDOUT — one of the only two modes that
//! use it (see cli/CONTRACT.md). A clean script prints nothing. The writer comes in from
//! main.zig: opening a terminal is the console layer's privilege, not an op's.
const std = @import("std");

const load = @import("load.zig");
const scriptCore = @import("core.zig");

/// Writes the diagnostics of `source` to `out`. Returns true when the script has an error.
pub fn checkInto(out: *std.Io.Writer, source: []const u8, label: []const u8) !bool {
    var script = scriptCore.Script.parse(source) catch return load.Error.ScriptUnreadable;
    defer script.deinit();
    try load.writeDiagnostics(out, script, label, .editor);
    return script.hasErrors();
}

pub fn run(gpa: std.mem.Allocator, io: std.Io, out: *std.Io.Writer, path: []const u8) !void {
    const source = try load.readScript(gpa, io, path);
    defer gpa.free(source);

    const failed = try checkInto(out, source, load.labelFor(path));
    try out.flush();
    if (failed) return load.Error.ScriptHasErrors;
}

test "checkInto prints one line per diagnostic and reports the error state" {
    var buf: [1024]u8 = undefined;
    var stream: std.Io.Writer = .fixed(&buf);
    const failed = try checkInto(&stream, "@source a.png:\n  @crp 10%\n", "a.stc");
    try std.testing.expect(failed);
    try std.testing.expect(std.mem.startsWith(u8, stream.buffered(), "a.stc:2:3: error: "));
    try std.testing.expect(std.mem.endsWith(u8, stream.buffered(), "[E_UNKNOWN_DIRECTIVE]\n"));
}

test "a clean script prints nothing and does not fail" {
    var buf: [1024]u8 = undefined;
    var stream: std.Io.Writer = .fixed(&buf);
    const failed = try checkInto(&stream, "@source a.png:\n  @crop 10%\n  @save o.png\n", "a.stc");
    try std.testing.expect(!failed);
    try std.testing.expectEqualStrings("", stream.buffered());
}

test "a warning prints but does not fail the check" {
    var buf: [1024]u8 = undefined;
    var stream: std.Io.Writer = .fixed(&buf);
    const failed = try checkInto(&stream, "@source a.png:\n", "a.stc");
    try std.testing.expect(!failed);
    try std.testing.expect(std.mem.indexOf(u8, stream.buffered(), "warning: ") != null);
    try std.testing.expect(std.mem.indexOf(u8, stream.buffered(), "[W_EMPTY_BLOCK]") != null);
}

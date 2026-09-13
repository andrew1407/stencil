//! `--script-check <file>`: print every diagnostic and exit non-zero if any is an error.
//! Written for an editor to parse, so it goes to STDOUT — one of the only two modes that
//! use it (see cli/CONTRACT.md). A clean script prints nothing.
const std = @import("std");

const load = @import("load.zig");
const scriptCore = @import("../scriptCore.zig");

/// Writes the diagnostics of `source` to `out`. Returns true when the script has an error.
pub fn checkInto(out: anytype, source: []const u8, label: []const u8) !bool {
    var script = scriptCore.Script.parse(source) catch return load.Error.ScriptUnreadable;
    defer script.deinit();

    var i: u32 = 0;
    while (i < script.diagnosticCount()) : (i += 1) {
        const d = script.diagnostic(i) orelse continue;
        try load.formatDiagnostic(out, label, d);
    }
    return script.hasErrors();
}

pub fn run(gpa: std.mem.Allocator, io: std.Io, path: []const u8) !void {
    const source = try load.readScript(gpa, io, path);
    defer gpa.free(source);

    var buf: [4096]u8 = undefined;
    var stdout = std.Io.File.stdout().writerStreaming(io, &buf);
    const failed = try checkInto(&stdout.interface, source, load.labelFor(path));
    try stdout.interface.flush();
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

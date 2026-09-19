//! Reading a .stc into memory and reporting what the core made of it. `-` reads stdin, so
//! an adapter can pipe a script without writing a temp file.
const std = @import("std");

const confine = @import("../confine.zig");
const report = @import("../report.zig");
const scriptCore = @import("../scriptCore.zig");

pub const Error = error{ ScriptUnreadable, ScriptHasErrors };

pub const MAX_SCRIPT_BYTES: usize = 4 << 20;

/// Reads the script at `path`, or stdin when `path` is "-". Caller owns the bytes. Every
/// refusal says why on stderr: main.zig only exits 1, and the adapters parse the `error:` line.
pub fn readScript(gpa: std.mem.Allocator, io: std.Io, path: []const u8) ![]u8 {
    if (std.mem.eql(u8, path, "-")) {
        var buf: [4096]u8 = undefined;
        var stdin = std.Io.File.stdin().readerStreaming(io, &buf);
        return stdin.interface.allocRemaining(gpa, .limited(MAX_SCRIPT_BYTES)) catch |e|
            return readFailed(labelFor(path), e);
    }
    if (confine.hasParentTraversal(path)) {
        report.err("refusing to read a script that climbs out of the working directory: '{s}'\n", .{path});
        return Error.ScriptUnreadable;
    }
    return std.Io.Dir.cwd().readFileAlloc(io, path, gpa, .limited(MAX_SCRIPT_BYTES)) catch |e|
        return readFailed(path, e);
}

fn readFailed(label: []const u8, e: anyerror) Error {
    if (e == error.StreamTooLong) {
        report.err("that script is too large: {s} (the cap is {d} bytes)\n", .{ label, MAX_SCRIPT_BYTES });
    } else {
        report.err("cannot read the script {s} ({s})\n", .{ label, @errorName(e) });
    }
    return Error.ScriptUnreadable;
}

/// The two written diagnostic shapes: `label:line:col:` for an editor to parse (--script-check)
/// and `line:col:len:` for the shared corpus. Every other field, and their order, is the same.
pub const Style = enum { editor, corpus };

/// "file:line:col: error|warning: message [CODE]" — what --script-check prints and what
/// the editors parse. `label` is the name to show, not the path that was opened.
pub fn formatDiagnostic(out: *std.Io.Writer, label: []const u8, d: scriptCore.Diagnostic) !void {
    try writeDiagnostic(out, label, d, .editor);
}

fn writeDiagnostic(out: *std.Io.Writer, label: []const u8, d: scriptCore.Diagnostic, style: Style) !void {
    const sev = if (d.severity == .err) "error" else "warning";
    switch (style) {
        .editor => try out.print("{s}:{d}:{d}: {s}: {s} [{s}]\n", .{ label, d.line, d.col, sev, d.message, d.code }),
        .corpus => try out.print("{d}:{d}:{d}: {s}: {s} [{s}]\n", .{ d.line, d.col, d.len, sev, d.message, d.code }),
    }
}

/// Every diagnostic of `script`, in source order, in the shape `style` names.
pub fn writeDiagnostics(out: *std.Io.Writer, script: scriptCore.Script, label: []const u8, style: Style) !void {
    var i: u32 = 0;
    while (i < script.diagnosticCount()) : (i += 1) {
        const d = script.diagnostic(i) orelse continue;
        try writeDiagnostic(out, label, d, style);
    }
}

/// Prints every diagnostic to stderr, the way the rest of the CLI reports; true when the script has an
/// error and therefore must not run. logo.zig writes the severity prefix, so this never spells it.
pub fn reportDiagnostics(script: scriptCore.Script, label: []const u8) bool {
    var i: u32 = 0;
    while (i < script.diagnosticCount()) : (i += 1) {
        const d = script.diagnostic(i) orelse continue;
        const args = .{ label, d.line, d.col, d.message, d.code };
        if (d.severity == .err) {
            report.err("{s}:{d}:{d}: {s} [{s}]\n", args);
        } else {
            report.note("{s}:{d}:{d}: {s} [{s}]\n", args);
        }
    }
    return script.hasErrors();
}

/// The label a diagnostic carries: the path as given, or "<stdin>".
pub fn labelFor(path: []const u8) []const u8 {
    return if (std.mem.eql(u8, path, "-")) "<stdin>" else path;
}

test "labelFor names stdin" {
    try std.testing.expectEqualStrings("<stdin>", labelFor("-"));
    try std.testing.expectEqualStrings("a.stc", labelFor("a.stc"));
}

test "formatDiagnostic writes the line the editors parse" {
    var buf: [256]u8 = undefined;
    var stream: std.Io.Writer = .fixed(&buf);
    try formatDiagnostic(&stream, "a.stc", .{
        .severity = .err,
        .code = "E_UNKNOWN_DIRECTIVE",
        .line = 3,
        .col = 5,
        .len = 4,
        .message = "unknown directive '@crp'",
    });
    try std.testing.expectEqualStrings(
        "a.stc:3:5: error: unknown directive '@crp' [E_UNKNOWN_DIRECTIVE]\n",
        stream.buffered(),
    );
}

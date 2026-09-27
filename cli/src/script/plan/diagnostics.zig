//! The `--script-plan` envelope's diagnostics: the script's own, in source order, and the one a
//! planner refusal adds among them, spanning the directive it stopped at.
const std = @import("std");

const scriptCore = @import("../core.zig");
const sequence = @import("sequence.zig");

fn writeDiagnostic(js: *std.json.Stringify, d: scriptCore.Diagnostic) !void {
    try js.beginObject();
    try js.objectField("severity");
    try js.write(if (d.severity == .err) "error" else "warning");
    try js.objectField("code");
    try js.write(d.code);
    try js.objectField("line");
    try js.write(d.line);
    try js.objectField("col");
    try js.write(d.col);
    try js.objectField("len");
    try js.write(d.len);
    try js.objectField("message");
    try js.write(d.message);
    if (d.related) |span| {
        try js.objectField("related");
        try js.write(span);
    }
    try js.endObject();
}

pub fn write(js: *std.json.Stringify, script: scriptCore.Script, refused: ?scriptCore.Diagnostic) !void {
    try js.beginArray();
    var pending = refused;
    var i: u32 = 0;
    while (i < script.diagnosticCount()) : (i += 1) {
        const d = script.diagnostic(i) orelse continue;
        if (pending) |r| if (r.line < d.line) {
            try writeDiagnostic(js, r);
            pending = null;
        };
        try writeDiagnostic(js, d);
    }
    if (pending) |r| try writeDiagnostic(js, r);
    try js.endArray();
}

/// A refusal as the diagnostic the envelope reports, spanning the op's directive word.
pub fn ofRefusal(script: scriptCore.Script, r: sequence.Refusal) scriptCore.Diagnostic {
    const op = script.op(r.op).?;
    return .{ .severity = .err, .code = r.code, .line = op.line, .col = op.col, .len = @intCast(1 + @tagName(op.kind).len), .message = r.message };
}

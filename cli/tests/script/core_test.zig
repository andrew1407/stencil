//! The core's .stc script ABI through its Zig bridge: blocks, ops and diagnostics read back, and
//! a length resolved into pixels against the image it is given.
const std = @import("std");
const scriptCore = @import("../../src/script/core.zig");
const OpKind = scriptCore.OpKind;
const Script = scriptCore.Script;
const Severity = scriptCore.Severity;
const SourceKind = scriptCore.SourceKind;

test "parse reports blocks, ops and a clean diagnostic list" {
    var s = try Script.parse("@source a.png:\n  @crop 10%\n  @save out.png\n");
    defer s.deinit();
    try std.testing.expect(!s.hasErrors());
    try std.testing.expectEqual(@as(u32, 1), s.blockCount());
    try std.testing.expectEqual(@as(u32, 3), s.opCount());
    try std.testing.expectEqualStrings("a.png", s.block(0).?.source);
    try std.testing.expectEqual(SourceKind.file, s.block(0).?.kind);
    try std.testing.expectEqual(OpKind.crop, s.op(1).?.kind);
    try std.testing.expectEqualStrings("out.png", s.opStr(2, 0));
}

test "a bad directive carries its code, span and a suggestion" {
    var s = try Script.parse("@source a.png:\n  @crp 10%\n");
    defer s.deinit();
    try std.testing.expect(s.hasErrors());
    const d = s.diagnostic(0).?;
    try std.testing.expectEqual(Severity.err, d.severity);
    try std.testing.expectEqualStrings("E_UNKNOWN_DIRECTIVE", d.code);
    try std.testing.expectEqual(@as(u32, 2), d.line);
    try std.testing.expect(std.mem.indexOf(u8, d.message, "@crop") != null);
}

test "resolve turns a crop into pixels for the image it is given" {
    var s = try Script.parse("@source a.png:\n  @crop 10%\n");
    defer s.deinit();
    var buf: [16]f64 = undefined;
    const r = try s.resolve(1, 200, 100, 37.795, 37.795, &buf);
    try std.testing.expectEqual(@as(usize, 4), r.len);
    try std.testing.expectApproxEqAbs(@as(f64, 20), r[0], 0.001);
    try std.testing.expectApproxEqAbs(@as(f64, 160), r[2], 0.001);
}

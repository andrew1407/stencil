//! Typed Zig bridge over the core's .stc script ABI (../core/cliApi.h). A script is far
//! longer than core.zig's 4 KiB scratch, so `parse` heap-dupes its own NUL-terminated copy.
//! Every string handed back points into the handle and dies with it — copy before destroy.
const std = @import("std");

const c = @cImport({
    @cInclude("core/cliApi.h");
});

pub const TokenKind = enum(c_int) {
    comment = 0,
    directive = 1,
    keyword = 2,
    number = 3,
    unit = 4,
    color = 5,
    string = 6,
    param = 7,
    punct = 8,
    ident = 9,
    err = 10,
};

pub const OpKind = enum(c_int) {
    open = 0,
    frame = 1,
    crop = 2,
    filter = 3,
    line = 4,
    rect = 5,
    layout = 6,
    save = 7,
    undo = 8,
    redo = 9,
};

pub const SourceKind = enum(c_int) { project = 0, file = 1, url = 2, dir = 3, glob = 4 };

pub const Severity = enum(c_int) { err = 0, warning = 1 };

pub const Diagnostic = struct {
    severity: Severity,
    code: []const u8,
    line: u32,
    col: u32,
    len: u32,
    message: []const u8,
};

pub const Token = struct { kind: TokenKind, line: u32, col: u32, len: u32 };

pub const Block = struct {
    source: []const u8,
    kind: SourceKind,
    frame: u32,
    op_start: u32,
    op_count: u32,
};

pub const Op = struct {
    kind: OpKind,
    block: u32,
    edit_index: u32,
    line: u32,
    col: u32,
    str_count: u32,
    num_count: u32,
};

pub const Error = error{ParseFailed, ResolveFailed};

/// A parsed script. `deinit` frees the core handle; every slice this returns dies with it.
pub const Script = struct {
    handle: c_int,

    pub fn parse(text: []const u8) Error!Script {
        const h = c.stencil_cli_scriptParse(text.ptr, @intCast(text.len));
        if (h == 0) return Error.ParseFailed;
        return .{ .handle = h };
    }

    pub fn deinit(self: *Script) void {
        c.stencil_cli_scriptDestroy(self.handle);
        self.handle = 0;
    }

    pub fn errorCount(self: Script) u32 {
        const n = c.stencil_cli_scriptErrorCount(self.handle);
        return if (n < 0) 0 else @intCast(n);
    }

    pub fn hasErrors(self: Script) bool {
        return self.errorCount() > 0;
    }

    pub fn diagnosticCount(self: Script) u32 {
        const n = c.stencil_cli_scriptDiagCount(self.handle);
        return if (n < 0) 0 else @intCast(n);
    }

    pub fn diagnostic(self: Script, i: u32) ?Diagnostic {
        var sev: c_int = 0;
        var line: c_int = 0;
        var col: c_int = 0;
        var len: c_int = 0;
        var code: [*c]const u8 = null;
        const msg = c.stencil_cli_scriptDiagAt(self.handle, @intCast(i), &sev, &line, &col, &len, &code);
        if (msg == null or code == null) return null;
        return .{
            .severity = @enumFromInt(sev),
            .code = std.mem.span(code),
            .line = @intCast(line),
            .col = @intCast(col),
            .len = @intCast(len),
            .message = std.mem.span(msg),
        };
    }

    pub fn tokenCount(self: Script) u32 {
        const n = c.stencil_cli_scriptTokenCount(self.handle);
        return if (n < 0) 0 else @intCast(n);
    }

    pub fn token(self: Script, i: u32) ?Token {
        var kind: c_int = 0;
        var line: c_int = 0;
        var col: c_int = 0;
        var len: c_int = 0;
        if (c.stencil_cli_scriptTokenAt(self.handle, @intCast(i), &kind, &line, &col, &len) == 0) return null;
        return .{ .kind = @enumFromInt(kind), .line = @intCast(line), .col = @intCast(col), .len = @intCast(len) };
    }

    pub fn blockCount(self: Script) u32 {
        const n = c.stencil_cli_scriptBlockCount(self.handle);
        return if (n < 0) 0 else @intCast(n);
    }

    pub fn block(self: Script, i: u32) ?Block {
        var kind: c_int = 0;
        var frame: c_int = 0;
        var start: c_int = 0;
        var count: c_int = 0;
        const spec = c.stencil_cli_scriptBlockAt(self.handle, @intCast(i), &kind, &frame, &start, &count);
        if (spec == null) return null;
        return .{
            .source = std.mem.span(spec),
            .kind = @enumFromInt(kind),
            .frame = @intCast(frame),
            .op_start = @intCast(start),
            .op_count = @intCast(count),
        };
    }

    pub fn opCount(self: Script) u32 {
        const n = c.stencil_cli_scriptOpCount(self.handle);
        return if (n < 0) 0 else @intCast(n);
    }

    pub fn op(self: Script, i: u32) ?Op {
        var kind: c_int = 0;
        var blk: c_int = 0;
        var edit: c_int = 0;
        var line: c_int = 0;
        var col: c_int = 0;
        var strs: c_int = 0;
        var nums: c_int = 0;
        if (c.stencil_cli_scriptOpAt(self.handle, @intCast(i), &kind, &blk, &edit, &line, &col, &strs, &nums) == 0)
            return null;
        return .{
            .kind = @enumFromInt(kind),
            .block = @intCast(blk),
            .edit_index = @intCast(edit),
            .line = @intCast(line),
            .col = @intCast(col),
            .str_count = @intCast(strs),
            .num_count = @intCast(nums),
        };
    }

    pub fn opStr(self: Script, i: u32, k: u32) []const u8 {
        const s = c.stencil_cli_scriptOpStr(self.handle, @intCast(i), @intCast(k));
        return if (s == null) "" else std.mem.span(s);
    }

    pub fn opTokCount(self: Script, i: u32) u32 {
        const n = c.stencil_cli_scriptOpTokCount(self.handle, @intCast(i));
        return if (n < 0) 0 else @intCast(n);
    }

    /// One UNRESOLVED length token ("10%", "-1in"); `resolve` turns them into pixels.
    pub fn opTok(self: Script, i: u32, k: u32) []const u8 {
        const s = c.stencil_cli_scriptOpTok(self.handle, @intCast(i), @intCast(k));
        return if (s == null) "" else std.mem.span(s);
    }

    pub fn opNum(self: Script, i: u32, k: u32) ?f64 {
        var v: f64 = 0;
        if (c.stencil_cli_scriptOpNum(self.handle, @intCast(i), @intCast(k), &v) == 0) return null;
        return v;
    }

    /// Length tokens -> pixels against the size the caller holds right now. `out` must be
    /// big enough for the op's layout (see cliApi.h); returns the slice actually written.
    pub fn resolve(self: Script, i: u32, w: f64, h: f64, px_per_cm_x: f64, px_per_cm_y: f64, out: []f64) Error![]f64 {
        const n = c.stencil_cli_scriptOpResolve(self.handle, @intCast(i), w, h, px_per_cm_x, px_per_cm_y, out.ptr, @intCast(out.len));
        if (n < 0) return Error.ResolveFailed;
        return out[0..@intCast(n)];
    }

    pub fn dump(self: Script) []const u8 {
        const s = c.stencil_cli_scriptDump(self.handle);
        return if (s == null) "" else std.mem.span(s);
    }
};

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

//! A `@crop` as a plan's `crop` action, in tokens an executor reads exactly as `--script` does:
//! cm, mm and in go out as the px they make at PX_PER_CM (an editor would size them by its page),
//! and a crop naming one axis gains the other one `--script` derives from the image itself.
//! `%` and px stay as written, so a plan with no size to resolve against still crops right.
const std = @import("std");

const scriptCore = @import("../core.zig");
const Dims = @import("sequence.zig").Dims;

const Value = std.json.Value;
const ObjectMap = std.json.ObjectMap;

/// core/parse/lengthTokens.cpp's absolute units: `value * mul / div` cm.
const Unit = struct { word: []const u8, mul: f64, div: f64 };
const CM_UNITS = [_]Unit{
    .{ .word = "cm", .mul = 1.0, .div = 1.0 },
    .{ .word = "mm", .mul = 1.0, .div = 10.0 },
    .{ .word = "in", .mul = 2.54, .div = 1.0 },
};

const KEYS = [_][]const u8{ "x1", "x2", "y1", "y2" };

/// A lowered length token: `-` (from the far end), the number, the unit as the lowerer wrote it.
const Token = struct { from_end: bool, value: f64, unit: []const u8 };

fn split(tok: []const u8) ?Token {
    const from_end = tok.len != 0 and tok[0] == '-';
    const start: usize = @intFromBool(from_end);
    var end = start;
    while (end < tok.len and (std.ascii.isDigit(tok[end]) or tok[end] == '.')) end += 1;
    const value = std.fmt.parseFloat(f64, tok[start..end]) catch return null;
    return .{ .from_end = from_end, .value = value, .unit = tok[end..] };
}

fn cmUnit(word: []const u8) ?Unit {
    for (CM_UNITS) |u| if (std.mem.eql(u8, word, u.word)) return u;
    return null;
}

/// An edge token with an absolute unit rewritten as the px `--script` resolves it to.
fn pxToken(a: std.mem.Allocator, tok: []const u8) ![]const u8 {
    const t = split(tok) orelse return tok;
    const u = cmUnit(t.unit) orelse return tok;
    const px = t.value * u.mul / u.div * scriptCore.PX_PER_CM;
    return std.fmt.allocPrint(a, "{s}{d}px", .{ if (t.from_end) "-" else "", px });
}

/// core resolveAxisPx: a token's position on an axis `len` long, a bare number moving `base`.
fn axisPx(tok: []const u8, len: f64, base: f64) ?f64 {
    const t = split(tok) orelse return null;
    if (t.unit.len == 0) return base + (if (t.from_end) -t.value else t.value);
    const px = if (std.mem.eql(u8, t.unit, "px"))
        t.value
    else if (std.mem.eql(u8, t.unit, "%"))
        (t.value / 100.0) * len
    else if (cmUnit(t.unit)) |u| t.value * u.mul / u.div * scriptCore.PX_PER_CM else return null;
    return if (t.from_end) len - px else px;
}

/// The far edge of the axis a one-axis crop leaves out (core resolveCropRect): it starts at 0
/// and spans the given axis at the page's aspect, the page being the image at PX_PER_CM.
fn derivedEdge(script: scriptCore.Script, i: u32, d: Dims, x_given: bool, album: bool) ?f64 {
    const page_w = d.w / scriptCore.PX_PER_CM;
    const page_h = d.h / scriptCore.PX_PER_CM;
    const lo = @min(page_w, page_h);
    const hi = @max(page_w, page_h);
    var aspect: f64 = if (lo <= 0.0 or hi <= 0.0) 1.0 else if (album) hi / lo else lo / hi;
    if (aspect <= 0.0) aspect = 1.0;
    const len = if (x_given) d.w else d.h;
    const first: u32 = if (x_given) 0 else 2;
    const t1 = script.opTok(i, first);
    const t2 = script.opTok(i, first + 1);
    const e1 = if (t1.len == 0) 0.0 else axisPx(t1, len, 0.0) orelse return null;
    const e2 = if (t2.len == 0) len else axisPx(t2, len, len) orelse return null;
    return if (x_given) @abs(e2 - e1) / aspect else @abs(e2 - e1) * aspect;
}

/// Op `i` as a `crop` action against the `dims` it cuts, when a probe gave any.
pub fn cropAction(a: std.mem.Allocator, script: scriptCore.Script, i: u32, dims: ?Dims) !Value {
    const x_given = script.opTok(i, 0).len != 0 or script.opTok(i, 1).len != 0;
    const y_given = script.opTok(i, 2).len != 0 or script.opTok(i, 3).len != 0;
    const album = (script.opNum(i, 0) orelse 0) != 0;
    const far: ?f64 = if (x_given == y_given) null else if (dims) |d| derivedEdge(script, i, d, x_given, album) else null;

    var spec: ObjectMap = .empty;
    for (KEYS, 0..) |key, k| {
        const tok = script.opTok(i, @intCast(k));
        if (tok.len != 0) {
            try spec.put(a, key, .{ .string = try pxToken(a, tok) });
        } else if (far) |f| {
            const edge = if (k % 2 == 0) "0px" else try std.fmt.allocPrint(a, "{d}px", .{f});
            try spec.put(a, key, .{ .string = edge });
        }
    }
    const aspect = script.opStr(i, 0);
    if (aspect.len != 0) try spec.put(a, "aspect", .{ .string = aspect });
    // Album only picks the derived axis's aspect; once that axis is written out it has no say.
    if (album and far == null) try spec.put(a, "album", .{ .bool = true });
    var m: ObjectMap = .empty;
    try m.put(a, "op", .{ .string = "crop" });
    try m.put(a, "spec", .{ .object = spec });
    return .{ .object = m };
}

const testing = std.testing;

/// The spec borrows the script's strings, so both live until `deinit`.
const Probe = struct {
    arena: std.heap.ArenaAllocator,
    script: scriptCore.Script,
    spec: ObjectMap = .empty,

    fn init(self: *Probe, text: []const u8, dims: ?Dims) !void {
        self.* = .{ .arena = .init(testing.allocator), .script = try scriptCore.Script.parse(text) };
        const v = try cropAction(self.arena.allocator(), self.script, 1, dims);
        self.spec = v.object.get("spec").?.object;
    }

    fn deinit(self: *Probe) void {
        self.script.deinit();
        self.arena.deinit();
    }
};

test "an absolute unit goes out as the px --script resolves it to, its far-edge sign kept" {
    var r: Probe = undefined;
    try r.init("@source a.png:\n  @crop x1=1cm x2=-5mm y1=10% y2=-2px\n", null);
    defer r.deinit();
    try testing.expectEqualStrings("37.79527559055118px", r.spec.get("x1").?.string);
    try testing.expectEqualStrings("-18.89763779527559px", r.spec.get("x2").?.string);
    try testing.expectEqualStrings("10%", r.spec.get("y1").?.string);
    try testing.expectEqualStrings("-2px", r.spec.get("y2").?.string);
}

test "a one-axis crop gains the axis --script derives, and album then has no say" {
    var r: Probe = undefined;
    try r.init("@source a.png:\n  @crop x1=10% x2=-10% album\n", .{ .w = 200, .h = 100 });
    defer r.deinit();
    try testing.expectEqualStrings("0px", r.spec.get("y1").?.string);
    try testing.expectEqualStrings("80px", r.spec.get("y2").?.string); // 160 wide at 2:1
    try testing.expect(r.spec.get("album") == null);
    var none: Probe = undefined;
    try none.init("@source a.png:\n  @crop y2=50% album\n", null);
    defer none.deinit();
    try testing.expect(none.spec.get("x1") == null); // nothing to derive from
    try testing.expect(none.spec.get("album").?.bool);
}

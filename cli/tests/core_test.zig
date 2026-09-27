//! The core bridge through the ABI: colours by name and hex, the expire vocabulary, durations,
//! page sizes and formats, the blank fill, and the invert and contour filters.
const std = @import("std");
const core = @import("../src/core.zig");
const applyContour = core.applyContour;
const applyFilter = core.applyFilter;
const canonicalPageFormat = core.canonicalPageFormat;
const colorNameAt = core.colorNameAt;
const colorNameCount = core.colorNameCount;
const durationOffHelp = core.durationOffHelp;
const durationUnitsHelp = core.durationUnitsHelp;
const fillRGBA = core.fillRGBA;
const namedPageSize = core.namedPageSize;
const pageFormats = core.pageFormats;
const parseColor = core.parseColor;
const parseDuration = core.parseDuration;
const zstr = core.zstr;
const testing = std.testing;

test "parseColor: names, hex, rejects junk" {
    const red = parseColor("red").?;
    try testing.expectEqual(@as(u8, 255), red.r);
    try testing.expectEqual(@as(u8, 0), red.g);
    try testing.expect(parseColor("#0000ff").?.b == 255);
    try testing.expect(parseColor("notacolour") == null);
}

test "the core's expire vocabulary joins into the console help lists" {
    try testing.expectEqualStrings("day | week | fortnight | month | year", durationUnitsHelp());
    try testing.expectEqualStrings("off | never | none", durationOffHelp());
}

test "colour-name enumeration is alphabetical and matches parseColor" {
    try testing.expect(colorNameCount() > 100);
    const first = colorNameAt(0).?;
    try testing.expectEqualStrings("aliceblue", first.name);
    try testing.expectEqual(@as(u32, 0xf0f8ff), first.rgb);
    try testing.expect(colorNameAt(colorNameCount()) == null);
    const got = parseColor(first.name).?;
    try testing.expectEqual(@as(u8, 0xf0), got.r);
}

test "parseDuration through the ABI" {
    const day: i64 = 24 * 60 * 60 * 1000;
    try testing.expectEqual(day, parseDuration("day").?);
    try testing.expectEqual(@as(i64, 23) * day, parseDuration("days 23").?);
    try testing.expectEqual(@as(i64, 3) * 30 * day, parseDuration("months 3").?);
    try testing.expectEqual(@as(i64, 14) * day, parseDuration("fortnight").?);
    try testing.expectEqual(@as(i64, 0), parseDuration("off").?); // keep forever
    try testing.expect(parseDuration("banana") == null);
    try testing.expect(parseDuration("days 0") == null);
}

test "namedPageSize + blank fill round trips through the ABI" {
    const p = namedPageSize("A4").?;
    try testing.expectApproxEqAbs(@as(f64, 21.0), p.w, 0.01);
    var px = [_]u8{0} ** 8;
    fillRGBA(&px, 2, .{ .r = 10, .g = 20, .b = 30, .a = 40 });
    try testing.expectEqual(@as(u8, 10), px[0]);
    try testing.expectEqual(@as(u8, 40), px[7]);
}

test "pageFormats lists the canonical names; canonicalPageFormat normalizes case" {
    const names = pageFormats();
    try testing.expect(std.mem.startsWith(u8, names, "A0 A1 "));
    try testing.expect(std.mem.indexOf(u8, names, "B5") != null);
    try testing.expect(std.mem.indexOf(u8, names, "C10") != null);
    try testing.expect(std.mem.indexOf(u8, names, "custom") == null);

    try testing.expectEqualStrings("B5", canonicalPageFormat("b5").?);
    try testing.expectEqualStrings("A10", canonicalPageFormat("a10").?);
    try testing.expect(canonicalPageFormat("custom") == null);
    try testing.expect(canonicalPageFormat("nope") == null);
    try testing.expect(canonicalPageFormat("") == null);
    // Every canonical name resolves to a size through the ABI.
    const b5 = namedPageSize(zstr(canonicalPageFormat("B5").?).?).?;
    try testing.expectApproxEqAbs(@as(f64, 17.6), b5.w, 0.01);
    try testing.expectApproxEqAbs(@as(f64, 25.0), b5.h, 0.01);
}

test "invert + contour filters through the ABI" {
    // Invert flips each channel; alpha untouched.
    var px = [_]u8{ 10, 20, 30, 40 };
    applyFilter("invert", &px, 1, .{ .r = 0, .g = 0, .b = 0, .a = 255 });
    try testing.expectEqual(@as(u8, 245), px[0]);
    try testing.expectEqual(@as(u8, 235), px[1]);
    try testing.expectEqual(@as(u8, 225), px[2]);
    try testing.expectEqual(@as(u8, 40), px[3]);
    // Contour on a flat image finds no edges -> uniform white, alpha preserved.
    var flat = [_]u8{ 90, 90, 90, 200 } ** 4; // 2x2, all identical
    applyContour(&flat, 2, 2);
    try testing.expectEqual(@as(u8, 255), flat[0]);
    try testing.expectEqual(@as(u8, 255), flat[1]);
    try testing.expectEqual(@as(u8, 255), flat[2]);
    try testing.expectEqual(@as(u8, 200), flat[3]);
}

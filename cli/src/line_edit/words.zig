//! Word boundaries for the word-wise moves and deletes, and the common prefix tab completion
//! fills in.
const setCommand = @import("../line_edit.zig").setCommand;
const std = @import("std");
const testing = std.testing;

// A word character for cursor motion: anything non-whitespace. Word jumps skip a run of
// separators, then the run of word characters (bash/emacs-style).
fn isWordChar(c: u8) bool {
    return c > ' ' and c != 0x7f;
}

/// One word to the LEFT of `pos`: back over separators, then over the word. 0 at line start.
pub fn wordLeft(line: []const u8, pos: usize) usize {
    var p = @min(pos, line.len);
    while (p > 0 and !isWordChar(line[p - 1])) p -= 1;
    while (p > 0 and isWordChar(line[p - 1])) p -= 1;
    return p;
}

/// One word to the RIGHT of `pos`: forward over separators, then over the word. `line.len` at end.
pub fn wordRight(line: []const u8, pos: usize) usize {
    var p = @min(pos, line.len);
    while (p < line.len and !isWordChar(line[p])) p += 1;
    while (p < line.len and isWordChar(line[p])) p += 1;
    return p;
}

// Length of the common case-insensitive prefix of `a` and `b`.
pub fn commonLen(a: []const u8, b: []const u8) usize {
    const n = @min(a.len, b.len);
    var i: usize = 0;
    while (i < n and std.ascii.toLower(a[i]) == std.ascii.toLower(b[i])) : (i += 1) {}
    return i;
}

pub fn copyInto(buf: []u8, src: []const u8) usize {
    const n = @min(buf.len, src.len);
    @memcpy(buf[0..n], src[0..n]);
    return n;
}

test "completion helpers: common prefix and command fill-in" {
    try testing.expectEqual(@as(usize, 2), commonLen("reset", "redo")); // "re"
    try testing.expectEqual(@as(usize, 0), commonLen("crop", "save"));
    try testing.expectEqual(@as(usize, 6), commonLen("ROTATE", "rotate")); // case-insensitive

    var buf: [32]u8 = undefined;
    try testing.expectEqualStrings("/upload ", buf[0..setCommand(&buf, true, "upload", true)]);
    try testing.expectEqualStrings("rotate", buf[0..setCommand(&buf, false, "rotate", false)]);
}

test "wordLeft/wordRight: jump over separator runs then the word" {
    const s = "/crop 10 20 to end";
    //         0123456789...
    try testing.expectEqual(@as(usize, 12), wordLeft(s, 14)); // inside "to" → start of "to"
    try testing.expectEqual(@as(usize, 9), wordLeft(s, 11)); // start of "20"
    try testing.expectEqual(@as(usize, 0), wordLeft(s, 5)); // from the space back to line start
    try testing.expectEqual(@as(usize, 0), wordLeft(s, 0)); // already home

    try testing.expectEqual(@as(usize, 5), wordRight(s, 0)); // over "/crop" to the space's end... "/crop"
    try testing.expectEqual(@as(usize, 8), wordRight(s, 5)); // over " 10"
    try testing.expectEqual(@as(usize, s.len), wordRight(s, 15)); // "end" → end of line
    try testing.expectEqual(@as(usize, s.len), wordRight(s, s.len)); // already at end
}

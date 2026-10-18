//! The skins table, read at compile time from the canonical common/config/console/skins.json
//! (embedded by build.zig) the way brand.zig reads its colours, so no word, phrase or colour
//! is retyped here; tests/config/skins_drift_test.zig re-parses the same bytes at run time.
const std = @import("std");
const skin = @import("../skin.zig");

const asset = @embedFile("skins.json");
const quota = asset.len * 64;

/// The `/eastereggs` word.
pub const list_word = str(rowOf("", 0), "listWord").?;

pub const traits = blk: {
    @setEvalBranchQuota(quota);
    var t = std.EnumArray(skin.Skin, skin.Traits).initFill(.{});
    for (std.enums.values(skin.Skin)) |s| {
        if (s != .none) t.set(s, fromJson(@tagName(s)));
    }
    break :blk t;
};

fn fromJson(comptime key: []const u8) skin.Traits {
    @setEvalBranchQuota(quota);
    const row = rowOf(key, std.mem.indexOf(u8, asset, "\"skins\": {").?);
    var t = skin.Traits{
        .word = str(row, "word") orelse @compileError("skins.json: " ++ key ++ " has no word"),
        .phrase = str(row, "phrase") orelse "",
        .phrase_off = str(row, "phraseOff") orelse "",
        .wordmark = str(row, "wordmark") orelse "S T E N C I L",
        .wordmark_accented = flag(row, "wordmarkAccented"),
        .animated = flag(row, "animated"),
        .moves_logo = flag(row, "movesLogo"),
        .paints_runs = flag(row, "paintsRuns"),
        .slides = flag(row, "slides"),
        .paints_cells = flag(row, "paintsCells"),
        .replaces_letters = flag(row, "replacesLetters"),
        .wash = if (str(row, "wash")) |hex| rgb(hex) else null,
    };
    if (t.paints_cells) {
        const fg = str(row, "fg").?;
        const bg = str(row, "bg").?;
        t.base = sgr(rgb(fg), rgb(bg));
        t.title = sgr(rgb(str(row, "titleFg").?), rgb(str(row, "titleBg").?));
        t.osc = std.fmt.comptimePrint("\x1b]10;{s}\x07\x1b]11;{s}\x07", .{ fg, bg });
    }
    return t;
}

// The `"key": {…}` object at or after `from`: json.dump's two-space layout closes a skin row
// with a line of four spaces and a brace, so the row never runs into the next.
fn rowOf(comptime key: []const u8, comptime from: usize) []const u8 {
    if (key.len == 0) return asset;
    const needle = "\"" ++ key ++ "\": {";
    const at = std.mem.indexOfPos(u8, asset, from, needle).? + needle.len;
    const end = std.mem.indexOfPos(u8, asset, at, "\n    }").?;
    return asset[at..end];
}

fn str(comptime row: []const u8, comptime key: []const u8) ?[]const u8 {
    const needle = "\"" ++ key ++ "\": \"";
    const at = (std.mem.indexOf(u8, row, needle) orelse return null) + needle.len;
    var end = at;
    while (row[end] != '"') : (end += 1) {
        if (row[end] == '\\') end += 1;
    }
    return unescape(row[at..end]);
}

// JSON's `\n`, `\"` and `\\` escapes; the table uses no other.
fn unescape(comptime s: []const u8) []const u8 {
    var out: []const u8 = "";
    var i: usize = 0;
    while (i < s.len) : (i += 1) {
        if (s[i] != '\\') {
            out = out ++ s[i .. i + 1];
            continue;
        }
        i += 1;
        out = out ++ switch (s[i]) {
            'n' => "\n",
            else => s[i .. i + 1],
        };
    }
    return out;
}

fn flag(comptime row: []const u8, comptime key: []const u8) bool {
    return std.mem.indexOf(u8, row, "\"" ++ key ++ "\": true") != null;
}

fn rgb(comptime hex: []const u8) [3]u8 {
    return .{ byteOf(hex[1..3]), byteOf(hex[3..5]), byteOf(hex[5..7]) };
}

fn byteOf(comptime s: []const u8) u8 {
    return std.fmt.parseInt(u8, s, 16) catch @compileError("skins.json: bad hex " ++ s);
}

// The plain cell style: a reset, then fg + bg, so an erase-to-EOL fills its bg.
fn sgr(comptime fg: [3]u8, comptime bg: [3]u8) []const u8 {
    return std.fmt.comptimePrint("\x1b[0;38;2;{d};{d};{d};48;2;{d};{d};{d}m", .{ fg[0], fg[1], fg[2], bg[0], bg[1], bg[2] });
}

const testing = std.testing;

test "table: every skin but none has a word, and the cell painters carry their colours" {
    for (std.enums.values(skin.Skin)) |s| {
        const t = traits.get(s);
        try testing.expectEqual(s != .none, t.word.len != 0);
        try testing.expectEqual(t.paints_cells, t.base.len != 0 and t.title.len != 0);
    }
    try testing.expectEqualStrings("eastereggs", list_word);
    try testing.expectEqualStrings("\x1b[0;38;2;35;209;139;48;2;0;0;0m", traits.get(.matrix).base);
    try testing.expectEqualStrings("\x1b]10;#23d18b\x07\x1b]11;#000000\x07", traits.get(.matrix).osc);
    try testing.expectEqualStrings("M I D G A R D", traits.get(.bifrost).wordmark);
    try testing.expectEqualStrings("Meow(", traits.get(.meow).phrase_off);
}

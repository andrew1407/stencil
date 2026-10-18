//! Drift guard for the secret skins: src/app/skin/table.zig scans the embedded
//! common/config/console/skins.json at COMPILE time, so this re-reads the same bytes with
//! std.json and pins every trait, picture, cat face, bulb and phrase the CLI keeps in code.
const std = @import("std");
const skin = @import("../../src/app/skin.zig");
const hues = @import("../../src/app/skin/hues.zig");
const pictures = @import("../../src/app/skin/pictures.zig");
const cat = @import("../../src/app/skin/cat.zig");
const msg = @import("../../src/app/messages.zig");
const testing = std.testing;

const skins_json = @embedFile("skins.json");

fn rgbOfHex(hex: []const u8) ![3]u8 {
    try testing.expect(hex.len == 7 and hex[0] == '#');
    return .{
        try std.fmt.parseInt(u8, hex[1..3], 16),
        try std.fmt.parseInt(u8, hex[3..5], 16),
        try std.fmt.parseInt(u8, hex[5..7], 16),
    };
}

fn text(row: std.json.ObjectMap, key: []const u8) []const u8 {
    return if (row.get(key)) |v| v.string else "";
}

fn flag(row: std.json.ObjectMap, key: []const u8) bool {
    return if (row.get(key)) |v| v.bool else false;
}

test "skin.zig's comptime table matches a real parse of skins.json, row for row" {
    const a = testing.allocator;
    const parsed = try std.json.parseFromSlice(std.json.Value, a, skins_json, .{});
    defer parsed.deinit();
    const root = parsed.value.object;
    try testing.expectEqualStrings(root.get("listWord").?.string, skin.list_word);
    try testing.expectEqual(@as(i64, skin.frame_ms), root.get("frameMs").?.integer);
    const rows = root.get("skins").?.object;
    try testing.expectEqual(std.enums.values(skin.Skin).len - 1, rows.count());
    for (std.enums.values(skin.Skin)) |s| {
        if (s == .none) continue;
        const row = (rows.get(@tagName(s)) orelse return error.MissingSkinRow).object;
        const t = skin.traitsOf(s);
        try testing.expectEqualStrings(text(row, "word"), t.word);
        try testing.expectEqualStrings(text(row, "phrase"), t.phrase);
        try testing.expectEqualStrings(text(row, "phraseOff"), t.phrase_off);
        try testing.expectEqualStrings(if (row.get("wordmark")) |v| v.string else "S T E N C I L", t.wordmark);
        try testing.expectEqual(flag(row, "wordmarkAccented"), t.wordmark_accented);
        try testing.expectEqual(flag(row, "animated"), t.animated);
        try testing.expectEqual(flag(row, "movesLogo"), t.moves_logo);
        try testing.expectEqual(flag(row, "paintsRuns"), t.paints_runs);
        try testing.expectEqual(flag(row, "slides"), t.slides);
        try testing.expectEqual(flag(row, "paintsCells"), t.paints_cells);
        try testing.expectEqual(flag(row, "replacesLetters"), t.replaces_letters);
        if (row.get("wash")) |w| try testing.expectEqual(try rgbOfHex(w.string), t.wash.?) else try testing.expect(t.wash == null);
        if (t.paints_cells) {
            const fg = try rgbOfHex(text(row, "fg"));
            const bg = try rgbOfHex(text(row, "bg"));
            var buf: [64]u8 = undefined;
            try testing.expectEqualStrings(try std.fmt.bufPrint(&buf, "\x1b[0;38;2;{d};{d};{d};48;2;{d};{d};{d}m", .{ fg[0], fg[1], fg[2], bg[0], bg[1], bg[2] }), t.base);
            try testing.expectEqualStrings(try std.fmt.bufPrint(&buf, "\x1b]10;{s}\x07\x1b]11;{s}\x07", .{ text(row, "fg"), text(row, "bg") }), t.osc);
        }
    }
    const blue = rows.get("bluescreen").?.object;
    try testing.expectEqualStrings(blue.get("body").?.string, msg.bluescreen_body);
    try testing.expect(std.mem.indexOf(u8, msg.bluescreen_title, blue.get("title").?.string) != null);
    try testing.expectEqual(try rgbOfHex(root.get("gold").?.string), [3]u8{ 255, 215, 0 });
    try testing.expect(std.mem.indexOf(u8, skin.gold, "255;215;0") != null);
}

test "the pictures, cat faces and bulbs the skins draw are skins.json's" {
    const a = testing.allocator;
    const parsed = try std.json.parseFromSlice(std.json.Value, a, skins_json, .{});
    defer parsed.deinit();
    const root = parsed.value.object;
    const pics = root.get("pictures").?.object;
    const lists = .{ .{ "fruits", &pictures.fruits }, .{ "pies", &pictures.pies }, .{ "cats", &pictures.cats } };
    inline for (lists) |l| {
        const want = pics.get(l[0]).?.array.items;
        try testing.expectEqual(want.len, l[1].len);
        for (want, l[1]) |w, have| try testing.expectEqualStrings(w.string, have);
    }
    try testing.expectEqual(@as(i64, pictures.swap_frames), pics.get("swapFrames").?.integer);
    const c = root.get("cat").?.object;
    try testing.expectEqual(@as(i64, cat.leg_frames), c.get("legFrames").?.integer);
    try testing.expectEqual(@as(i64, cat.step_frames), c.get("stepFrames").?.integer);
    const styles = c.get("styles").?.array.items;
    try testing.expectEqual(styles.len, cat.styles.len);
    for (styles, cat.styles) |style, have| {
        for (style.array.items, have) |face, f| {
            try testing.expectEqualStrings(face.array.items[0].string, f.text);
            try testing.expectEqual(face.array.items[1].integer, @as(i64, @intCast(f.cols)));
        }
    }
    const h = root.get("hues").?.object;
    try testing.expectEqual(@as(i64, hues.hue_step), h.get("hueStep").?.integer);
    try testing.expectEqual(@as(i64, hues.bulb_frames), h.get("bulbFrames").?.integer);
    const bulbs = h.get("bulbs").?.array.items;
    try testing.expectEqual(bulbs.len, hues.bulbs.len);
    for (bulbs, hues.bulbs) |b, have| try testing.expectEqual(try rgbOfHex(b.string), have);
}

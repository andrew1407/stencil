//! Drift guard: src/params/uriKeys.zig holds common/config/launchQuery.json as Zig tables. This
//! walks the asset and fails on any spelling, secret, theme word, dock or boolean word the two
//! disagree on, so the CLI reads a launch URI with exactly the browser's vocabulary.
const std = @import("std");
const keys = @import("../../src/params/uriKeys.zig");
const testing = std.testing;

const asset = @embedFile("launchQuery.json");

fn expectList(arr: std.json.Array, want: []const []const u8) !void {
    try testing.expectEqual(want.len, arr.items.len);
    for (arr.items, want) |v, w| try testing.expectEqualStrings(w, v.string);
}

test "every spelling in launchQuery.json is in the table under its canonical key, and nothing else is" {
    const a = testing.allocator;
    const parsed = try std.json.parseFromSlice(std.json.Value, a, asset, .{});
    defer parsed.deinit();
    const root = parsed.value.object;

    var count: usize = 0;
    var it = root.get("keys").?.object.iterator();
    while (it.next()) |entry| {
        for (entry.value_ptr.array.items) |w| {
            count += 1;
            const canonical = keys.canonicalOf(w.string) orelse {
                std.debug.print("launchQuery.json spells '{s}', which uriKeys.zig does not know\n", .{w.string});
                return error.MissingSpelling;
            };
            try testing.expectEqualStrings(entry.key_ptr.*, canonical);
        }
    }
    try testing.expectEqual(count, keys.spellings.len);

    try expectList(root.get("secretKeys").?.array, &keys.secret_keys);
    try expectList(root.get("chatDocks").?.array, &keys.chat_docks);
    try expectList(root.get("trueWords").?.array, &keys.true_words);
    try expectList(root.get("falseWords").?.array, &keys.false_words);
    const themes = root.get("themeWords").?.object;
    try testing.expectEqual(themes.count(), keys.theme_words.len);
    for (keys.theme_words) |tw| try testing.expectEqualStrings(themes.get(tw[0]).?.string, tw[1]);
}

test "the script cap is constants.json LAUNCH.scriptMaxChars" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    try testing.expectEqual(@as(usize, 200000), keys.scriptMaxChars(arena.allocator()));
}

// Walks the shared §12.1 persisted-chat corpus (llm/fixtures/chatDoc/) against the
// cli's real reader/writer (llm.parseChatDoc / llm.chatDocAlloc — the pair the console's
// /chat persistence and .stencil chat blocks use). The cli parser is string-only, so
// object-form cases are serialized first (per the schema). parseChatDoc surfaces TURNS
// only — savedAt is not read back (the writer stamps the session clock), so roundtrips
// re-serialize with the doc's own savedAt and tolerance cases compare messages.
const std = @import("std");
const llm = @import("../src/llm.zig");
const fx = @import("fixture_corpus.zig");
const testing = std.testing;

fn savedAtOf(doc: std.json.Value) i64 {
    const v = fx.member(doc, "savedAt") orelse return 0;
    return if (v == .integer) v.integer else 0;
}

/// Compare parsed turns against a fixture "messages" array of {role,text}.
fn expectTurns(turns: []const llm.Turn, messages: std.json.Value) !void {
    try testing.expectEqual(messages.array.items.len, turns.len);
    for (messages.array.items, turns) |m, t| {
        try testing.expectEqualStrings(fx.memberStr(m, "role").?, @tagName(t.role));
        try testing.expectEqualStrings(fx.memberStr(m, "text").?, t.text);
    }
}

test "chatDoc corpus: roundtrip.json — parse ∘ serialize is the identity" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    const cases = try fx.loadJson(a, io, "llm/fixtures/chatDoc/roundtrip.json");
    for (cases.array.items) |case| {
        const doc = fx.member(case, "doc").?;
        const bytes = try fx.stringify(a, doc);

        const turns = try llm.parseChatDoc(testing.allocator, bytes);
        defer llm.freeTurns(testing.allocator, turns);
        try expectTurns(turns, fx.member(doc, "messages").?);

        // Re-serialize (with the doc's own savedAt) and compare structurally;
        // then reparse to confirm the fixed point.
        const out = try llm.chatDocAlloc(testing.allocator, turns, savedAtOf(doc));
        defer testing.allocator.free(out);
        const out_v = try std.json.parseFromSliceLeaky(std.json.Value, a, out, .{});
        try testing.expect(fx.jsonEquals(doc, out_v));

        const again = try llm.parseChatDoc(testing.allocator, out);
        defer llm.freeTurns(testing.allocator, again);
        try expectTurns(again, fx.member(doc, "messages").?);
    }
}

test "chatDoc corpus: tolerance.json — the lenient-read pins" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    const cases = try fx.loadJson(a, io, "llm/fixtures/chatDoc/tolerance.json");
    var walked: usize = 0;
    for (cases.array.items) |case| {
        walked += 1;
        const bytes = if (fx.memberStr(case, "docString")) |s|
            s
        else
            try fx.stringify(a, fx.member(case, "doc").?);

        const turns = try llm.parseChatDoc(testing.allocator, bytes);
        defer llm.freeTurns(testing.allocator, turns);

        const expected = fx.member(case, "expectParsed").?;
        if (expected == .null) {
            // "Treated as a missing document" — the cli reads that as zero turns,
            // never an error (parseChatDoc is total).
            try testing.expectEqual(@as(usize, 0), turns.len);
        } else {
            // The cli surfaces messages only; the savedAt pins (numeric-string
            // coercion, garbage→0) have no cli-observable seam — the writer stamps
            // the session clock instead of restoring the stored value.
            try expectTurns(turns, fx.member(expected, "messages").?);
        }
    }
    try testing.expectEqual(@as(usize, 17), walked);
}

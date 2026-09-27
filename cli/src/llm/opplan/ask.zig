//! The contract-§11 `ask` card: the one place a plan may ask the user a question instead of
//! acting. core/opplan validated it (a malformed card is a plan error); a console shows the
//! labels only, so an option's preview or picture never reaches the typed card.
const std = @import("std");
const opSchema = @import("../opSchema.zig");
const ObjectMap = opSchema.ObjectMap;
const testing = std.testing;
const model = @import("model.zig");
const validate = @import("validate.zig");
const Ask = model.Ask;
const AskOption = model.AskOption;
const Plan = model.Plan;
const parsePlan = validate.parsePlan;

/// Core's normalized card → the console's typed one (question and labels trimmed, defaults applied).
pub fn card(a: std.mem.Allocator, ask: ObjectMap) error{OutOfMemory}!Ask {
    var opts: std.ArrayList(AskOption) = .empty;
    for (ask.get("options").?.array.items) |o| try opts.append(a, .{ .label = o.object.get("label").?.string });
    return .{
        .question = ask.get("question").?.string,
        .multi = std.mem.eql(u8, ask.get("mode").?.string, "multi"),
        .allow_custom = ask.get("allowCustom").?.bool,
        .custom_label = ask.get("customLabel").?.string,
        .options = try opts.toOwnedSlice(a),
    };
}

/// Resolve what the user typed at an `ask` card (§11.3/§11.4: a console answers by NUMBER — "2", or
/// "1,3" when multi-select) into the joined labels. Null = not a selection, which is no error.
pub fn resolveAskAnswer(
    gpa: std.mem.Allocator,
    options: [][]u8,
    multi: bool,
    typed: []const u8,
) error{OutOfMemory}!?[]u8 {
    const t = std.mem.trim(u8, typed, " \t\r\n");
    if (t.len == 0 or options.len == 0) return null;

    var picked: std.ArrayList(usize) = .empty;
    defer picked.deinit(gpa);
    var it = std.mem.tokenizeAny(u8, t, ", \t");
    while (it.next()) |tok| {
        const n = std.fmt.parseInt(usize, tok, 10) catch return null; // not a selection → plain text
        if (n < 1 or n > options.len) return null; // a number nobody offered → plain text
        // A repeat is the user re-stating a pick, not a second one.
        if (std.mem.indexOfScalar(usize, picked.items, n - 1) == null) try picked.append(gpa, n - 1);
    }
    if (picked.items.len == 0) return null;
    if (picked.items.len > 1 and !multi) return null; // several numbers at a pick-one card

    var out: std.ArrayList(u8) = .empty;
    errdefer out.deinit(gpa);
    for (picked.items, 0..) |idx, i| {
        if (i != 0) try out.appendSlice(gpa, ", ");
        try out.appendSlice(gpa, options[idx]);
    }
    const max_answer: usize = @intFromFloat(opSchema.get().limitNamed("ask.answer"));
    if (out.items.len > max_answer) out.shrinkRetainingCapacity(max_answer);
    return try out.toOwnedSlice(gpa);
}

/// Parse a plan carrying an `ask`, returning the card (caller deinits the plan).
fn parseAsk(a: std.mem.Allocator, json: []const u8) !Plan {
    switch (try parsePlan(a, json)) {
        .invalid => |m| {
            a.free(m);
            return error.TestUnexpectedInvalid;
        },
        .plan => |p| return p,
    }
}

test "resolveAskAnswer: a number picks a label; anything else stays plain text" {
    const a = testing.allocator;
    var opts = [_][]u8{
        try a.dupe(u8, "Sepia"), try a.dupe(u8, "B&W"), try a.dupe(u8, "Blue"),
    };
    defer for (opts) |o| a.free(o);

    const one = (try resolveAskAnswer(a, &opts, false, " 2 ")).?;
    defer a.free(one);
    try testing.expectEqualStrings("B&W", one);

    // Several numbers at a pick-ONE card is not a selection — it goes as typed.
    try testing.expect((try resolveAskAnswer(a, &opts, false, "1,3")) == null);

    const many = (try resolveAskAnswer(a, &opts, true, "1, 3")).?;
    defer a.free(many);
    try testing.expectEqualStrings("Sepia, Blue", many);

    const spaced = (try resolveAskAnswer(a, &opts, true, "3 1")).?;
    defer a.free(spaced);
    try testing.expectEqualStrings("Blue, Sepia", spaced); // the user's order is kept

    const dedup = (try resolveAskAnswer(a, &opts, true, "2,2")).?;
    defer a.free(dedup);
    try testing.expectEqualStrings("B&W", dedup); // a repeat is one pick

    // Out of range, non-numeric, empty, and no card at all → plain text (never an error).
    try testing.expect((try resolveAskAnswer(a, &opts, false, "0")) == null);
    try testing.expect((try resolveAskAnswer(a, &opts, false, "4")) == null);
    try testing.expect((try resolveAskAnswer(a, &opts, true, "1,9")) == null);
    try testing.expect((try resolveAskAnswer(a, &opts, false, "make it warmer")) == null);
    try testing.expect((try resolveAskAnswer(a, &opts, false, "   ")) == null);
    var none = [_][]u8{};
    try testing.expect((try resolveAskAnswer(a, &none, false, "1")) == null);
}

test "parsePlan: ask card, defaults, and multi mode" {
    const a = testing.allocator;
    var p = try parseAsk(a,
        \\{"version":1,"reply":"pick one","ask":{"question":"Which tint?","options":[{"label":"Sepia"},{"label":"B&W"}]}}
    );
    defer p.deinit();
    const ask = p.ask.?;
    try testing.expectEqualStrings("Which tint?", ask.question);
    try testing.expect(!ask.multi and !ask.allow_custom); // default single
    try testing.expectEqualStrings(opSchema.get().default_custom_label, ask.custom_label);
    try testing.expectEqualStrings("Sepia", ask.options[0].label);

    var m = try parseAsk(a,
        \\{"version":1,"reply":"pick some","ask":{"question":" Which? ","mode":"multi","allowCustom":true,"customLabel":" Other ","options":[{"label":" A "},{"label":"B"}]}}
    );
    defer m.deinit();
    try testing.expect(m.ask.?.multi and m.ask.?.allow_custom);
    try testing.expectEqualStrings("Which?", m.ask.?.question); // trimmed
    try testing.expectEqualStrings("Other", m.ask.?.custom_label);
    try testing.expectEqualStrings("A", m.ask.?.options[0].label);
}

test "parsePlan: no ask on an ordinary or chat-only turn; a malformed card is a plan error" {
    const a = testing.allocator;
    var p = try parseAsk(a, "{\"version\":1,\"reply\":\"hi\",\"actions\":[]}");
    defer p.deinit();
    try testing.expect(p.ask == null);
    var c = try parseAsk(a, "just chatting");
    defer c.deinit();
    try testing.expect(c.ask == null and c.chat_only);
    try validate.mustReject("{\"reply\":\"x\",\"ask\":{\"question\":\"Q\",\"options\":[{\"label\":\"only\"}]}}", "Invalid plan: \"ask.options\" must hold 2..5 entries");
}

test "parsePlan: a console drops option previews with ONE note, never the option" {
    const a = testing.allocator;
    var p = try parseAsk(a,
        \\{"version":1,"reply":"pick","ask":{"question":"Which?","options":[{"label":"Sepia","actions":[{"op":"filter","mode":"sepia"}]},{"label":"Web","image":{"url":"https://e/x.png"}}]}}
    );
    defer p.deinit();
    try testing.expectEqual(@as(usize, 2), p.ask.?.options.len); // both kept
    var notes: usize = 0;
    for (p.warnings) |w| {
        if (std.mem.eql(u8, w, validate.previews_note)) notes += 1;
    }
    try testing.expectEqual(@as(usize, 1), notes); // one per CARD, not per option
}

test "parsePlan: a misplaced op in an option preview never costs the option (§1/§11.2)" {
    const a = testing.allocator;
    var p = try parseAsk(a,
        \\{"version":1,"reply":"pick","ask":{"question":"Which?","options":[{"label":"Wipe","actions":[{"op":"clear"}]},{"label":"Sepia","actions":[{"op":"filter","mode":"sepia"}]}]}}
    );
    defer p.deinit();
    try testing.expectEqual(@as(usize, 2), p.ask.?.options.len); // both kept, both previewless
    try testing.expectEqualStrings("Wipe", p.ask.?.options[0].label);
}

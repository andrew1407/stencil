//! The contract-§11 `ask` card: the one place a plan may ask the user a question instead of
//! acting. Its options carry PREVIEW actions only — a console drops them with one note —
//! and a malformed card is a plan error, never a silently ignored field.
const std = @import("std");
const opSchema = @import("../opSchema.zig");
const Diag = opSchema.Diag;
const Entry = opSchema.Entry;
const ObjectMap = opSchema.ObjectMap;
const Value = opSchema.Value;
const testing = std.testing;
const model = @import("model.zig");
const normalize = @import("normalize.zig");
const validate = @import("validate.zig");
const Ask = model.Ask;
const AskOption = model.AskOption;
const Plan = model.Plan;
const ValidateError = validate.ValidateError;
const parsePlan = validate.parsePlan;
const mustPlan = validate.mustPlan;
const mustReject = validate.mustReject;
const actions = @import("actions.zig");
const validateActions = actions.validateActions;
const nonNullField = actions.nonNullField;


/// Validate the optional `ask` object (contract §11) → the card, or null when absent.
/// The card's STRUCTURE (keys, caps, the image reference's exactly-one-of url /
/// projectId / scanIndex, http(s)-only urls) is the registry's ask schema; the preview
/// actions are validated as nested actions and then dropped — a console shows none.
pub fn validateAsk(
    a: std.mem.Allocator,
    value: ?std.json.Value,
    warnings: *std.ArrayList([]const u8),
    diag: *Diag,
) ValidateError!?Ask {
    const v = value orelse return null;
    if (v == .null) return null;
    const schema = opSchema.get();
    try schema.validateAsk(a, diag, v);
    const card = try schema.normalizeAsk(a, v.object);

    var opts: std.ArrayList(AskOption) = .empty;
    var dropped_preview = false;
    for (v.object.get("options").?.array.items, card.get("options").?.array.items, 0..) |raw_o, norm_o, i| {
        const oo = raw_o.object;
        if (nonNullField(oo, "actions") != null or nonNullField(oo, "image") != null) dropped_preview = true;
        // Preview actions are ordinary §2 actions "inside variants or previews": bad
        // params fail the plan; a misplaced op only costs the preview (§1/§11.2), which
        // this console never shows anyway.
        if (nonNullField(oo, "actions")) |acts| {
            var where_buf: [24]u8 = undefined;
            const where = std.fmt.bufPrint(&where_buf, "ask option {d}", .{i + 1}) catch "ask option";
            var misplaced: ?[]const u8 = null;
            _ = validateActions(a, acts, warnings, where, &misplaced, diag) catch |e| switch (e) {
                error.Misplaced => {},
                else => |other| return other,
            };
        }
        try opts.append(a, .{ .label = norm_o.object.get("label").?.string });
    }
    // One note for the whole card, not one per option: a terminal cannot show any of them.
    if (dropped_preview)
        try warnings.append(a, try a.dupe(u8, "the console can't show option previews — the choices are listed by name"));

    return .{
        .question = card.get("question").?.string,
        .multi = std.mem.eql(u8, card.get("mode").?.string, "multi"),
        .allow_custom = if (card.get("allowCustom")) |c| c.bool else false,
        .custom_label = if (card.get("customLabel")) |l| l.string else schema.default_custom_label,
        .options = try opts.toOwnedSlice(a),
    };
}

// §11 interactive replies (`ask`)

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

/// Assert a plan is REJECTED (a malformed card is a plan error, not a silent drop).
fn expectAskInvalid(a: std.mem.Allocator, json: []const u8) !void {
    switch (try parsePlan(a, json)) {
        .invalid => |m| a.free(m),
        .plan => |p| {
            var plan = p;
            plan.deinit();
            return error.TestExpectedInvalid;
        },
    }
}

test "parsePlan: ask card, defaults, and multi mode" {
    const a = testing.allocator;

    var p = try parseAsk(a,
        \\{"version":1,"reply":"pick one","ask":{"question":"Which tint?","options":[{"label":"Sepia"},{"label":"B&W"}]}}
    );
    defer p.deinit();
    const ask = p.ask.?;
    try testing.expectEqualStrings("Which tint?", ask.question);
    try testing.expect(!ask.multi); // default single
    try testing.expect(!ask.allow_custom);
    try testing.expectEqualStrings(opSchema.get().default_custom_label, ask.custom_label);
    try testing.expectEqual(@as(usize, 2), ask.options.len);
    try testing.expectEqualStrings("Sepia", ask.options[0].label);

    var m = try parseAsk(a,
        \\{"version":1,"reply":"pick some","ask":{"question":" Which? ","mode":"multi","allowCustom":true,"customLabel":" Other ","options":[{"label":" A "},{"label":"B"}]}}
    );
    defer m.deinit();
    try testing.expect(m.ask.?.multi);
    try testing.expect(m.ask.?.allow_custom);
    try testing.expectEqualStrings("Which?", m.ask.?.question); // trimmed
    try testing.expectEqualStrings("Other", m.ask.?.custom_label);
    try testing.expectEqualStrings("A", m.ask.?.options[0].label);
}

test "parsePlan: no ask on an ordinary or chat-only turn" {
    const a = testing.allocator;
    var p = try parseAsk(a,
        \\{"version":1,"reply":"hi","actions":[]}
    );
    defer p.deinit();
    try testing.expect(p.ask == null);

    var c = try parseAsk(a, "just chatting");
    defer c.deinit();
    try testing.expect(c.ask == null);
    try testing.expect(c.chat_only);
}

test "parsePlan: malformed ask cards are plan errors" {
    const a = testing.allocator;
    try expectAskInvalid(a,
        \\{"version":1,"reply":"x","ask":"hello"}
    );
    try expectAskInvalid(a,
        \\{"version":1,"reply":"x","ask":{"options":[{"label":"A"},{"label":"B"}]}}
    );
    try expectAskInvalid(a,
        \\{"version":1,"reply":"x","ask":{"question":"   ","options":[{"label":"A"},{"label":"B"}]}}
    );
    try expectAskInvalid(a,
        \\{"version":1,"reply":"x","ask":{"question":"Q","options":[]}}
    );
    try expectAskInvalid(a,
        \\{"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"only"}]}}
    );
    try expectAskInvalid(a,
        \\{"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"1"},{"label":"2"},{"label":"3"},{"label":"4"},{"label":"5"},{"label":"6"}]}}
    );
    try expectAskInvalid(a,
        \\{"version":1,"reply":"x","ask":{"question":"Q","mode":"maybe","options":[{"label":"A"},{"label":"B"}]}}
    );
    try expectAskInvalid(a,
        \\{"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A"},{"label":"B"}],"sneaky":1}}
    );
    try expectAskInvalid(a,
        \\{"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A","sneaky":1},{"label":"B"}]}}
    );
    try expectAskInvalid(a,
        \\{"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":""},{"label":"B"}]}}
    );
    try expectAskInvalid(a,
        \\{"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A","actions":[],"image":{"url":"https://e/x"}},{"label":"B"}]}}
    );
    try expectAskInvalid(a,
        \\{"version":1,"reply":"x","ask":{"question":"Q","options":[{"label":"A"},{"label":"B"}],"allowCustom":"yes"}}
    );
}


test "parsePlan: a console drops option previews with ONE note, never the option" {
    const a = testing.allocator;
    var p = try parseAsk(a,
        \\{"version":1,"reply":"pick","ask":{"question":"Which?","options":[{"label":"Sepia","actions":[{"op":"filter","mode":"sepia"}]},{"label":"Web","image":{"url":"https://e/x.png"}}]}}
    );
    defer p.deinit();
    try testing.expectEqual(@as(usize, 2), p.ask.?.options.len); // both kept
    try testing.expectEqualStrings("Sepia", p.ask.?.options[0].label);
    var notes: usize = 0;
    for (p.warnings) |w| {
        if (std.mem.indexOf(u8, w, "can't show option previews") != null) notes += 1;
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

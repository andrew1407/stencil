//! A model reply → a validated `Plan` (contract §3). The generic pass is table-driven from
//! opRegistry.json via opSchema.zig; anything it cannot prove is a rejection, and a
//! variant's misplaced op costs the variant, never the plan. The entries themselves are
//! checked in actions.zig and ask.zig.
const std = @import("std");
const opSchema = @import("../opSchema.zig");
const Diag = opSchema.Diag;
const Entry = opSchema.Entry;
const ObjectMap = opSchema.ObjectMap;
const Value = opSchema.Value;
const testing = std.testing;
const model = @import("model.zig");
const normalize = @import("normalize.zig");
const registry = @import("../registry.zig");
const op_registry = registry.op_registry;
const extract = @import("extract.zig");
const Plan = model.Plan;
const Variant = model.Variant;
const Action = model.Action;
const stripFences = extract.stripFences;
const firstJsonObject = extract.firstJsonObject;
const validateActions = @import("actions.zig").validateActions;
const validateAsk = @import("ask.zig").validateAsk;
const nonNullField = @import("actions.zig").nonNullField;

/// parsePlan's outcome: a validated plan, or a user-facing rejection message (gpa-owned).
pub const ParseOutcome = union(enum) {
    plan: Plan,
    invalid: []u8,
};

/// Parse the raw LLM reply into a validated plan (§1): strip fences, take the first balanced `{…}`
/// object (none → chat-only turn); unknown ops drop, bad params reject, a misplaced variant drops.
pub fn parsePlan(gpa: std.mem.Allocator, raw: []const u8) error{OutOfMemory}!ParseOutcome {
    var plan = Plan{ .arena = std.heap.ArenaAllocator.init(gpa) };
    errdefer plan.arena.deinit();
    const a = plan.arena.allocator();

    const stripped = try stripFences(a, raw);
    const object_src = firstJsonObject(stripped) orelse return chatOnly(&plan, a, raw);
    const root = std.json.parseFromSliceLeaky(std.json.Value, a, object_src, .{}) catch
        return chatOnly(&plan, a, raw); // braces that aren't actually JSON → chat-only
    if (root != .object) return chatOnly(&plan, a, raw);
    const obj = root.object;

    var diag = Diag{ .gpa = gpa };
    validateInto(&plan, a, obj, &diag) catch |e| switch (e) {
        error.Invalid => {
            plan.arena.deinit();
            return .{ .invalid = diag.msg.? };
        },
        error.OutOfMemory => {
            diag.deinit();
            return error.OutOfMemory;
        },
    };
    return .{ .plan = plan };
}

pub fn chatOnly(plan: *Plan, a: std.mem.Allocator, raw: []const u8) error{OutOfMemory}!ParseOutcome {
    plan.reply = try a.dupe(u8, std.mem.trim(u8, raw, " \t\r\n"));
    plan.chat_only = true;
    return .{ .plan = plan.* };
}

pub const ValidateError = error{ Invalid, OutOfMemory };
/// validateActions inside a variant/preview: `Misplaced` = drop that holder (§1).
pub const NestedError = ValidateError || error{Misplaced};

pub fn validateInto(plan: *Plan, a: std.mem.Allocator, obj: ObjectMap, diag: *Diag) ValidateError!void {
    const schema = opSchema.get();
    // `version` other than 1 (or absent) is accepted but ignored (contract §1).
    var warnings: std.ArrayList([]const u8) = .empty;
    // §1 reply tolerance: models routinely omit the reply while planning valid
    // actions — substitute rather than lose the plan to a missing pleasantry.
    const reply_v = obj.get("reply");
    const reply_omitted = reply_v == null or reply_v.? != .string or
        std.mem.trim(u8, reply_v.?.string, " \t\r\n").len == 0;
    if (reply_omitted) {
        plan.reply = ""; // filled in below, once the plan's contents are known
    } else {
        plan.reply = try a.dupe(u8, reply_v.?.string);
    }
    plan.actions = validateActions(a, obj.get("actions"), &warnings, "actions", null, diag) catch |e| switch (e) {
        error.Misplaced => unreachable, // only raised with a `misplaced` sink
        else => |other| return other,
    };

    var variants: std.ArrayList(Variant) = .empty;
    if (obj.get("variants")) |vv| {
        if (vv != .null) {
            // The registry envelope: ≤ MAX_VARIANTS objects of {label: string, actions}.
            try schema.checkEnvelope(a, diag, vv, "variants", "variants");
            for (vv.array.items, 0..) |raw_v, i| {
                const vo = raw_v.object;
                const label: []const u8 = if (nonNullField(vo, "label")) |lv| lv.string else "";
                var where_buf: [24]u8 = undefined;
                const where = std.fmt.bufPrint(&where_buf, "variant {d}", .{i + 1}) catch "variant";
                // §1: a variant carrying a top-level-only or console-settings op is
                // dropped with a warning naming it — the rest of the plan still runs.
                var misplaced: ?[]const u8 = null;
                const actions = validateActions(a, vo.get("actions"), &warnings, where, &misplaced, diag) catch |e| switch (e) {
                    error.Misplaced => {
                        try warnings.append(a, try droppedVariantWarning(a, i + 1, label, misplaced.?));
                        continue;
                    },
                    else => |other| return other,
                };
                // An absent/empty label stays empty — the executor's file naming falls
                // back to the variant's 1-based position ("variant-2.png").
                try variants.append(a, .{ .label = try a.dupe(u8, label), .actions = actions });
            }
        }
    }
    plan.variants = try variants.toOwnedSlice(a);
    plan.ask = try validateAsk(a, obj.get("ask"), &warnings, diag);
    // The substitute must not overstate what happened: "Done." only when the plan carries work, since a
    // bare "Done." on an empty plan reads as a success that never occurred (§1).
    if (reply_omitted) {
        if (plan.actions.len > 0 or plan.variants.len > 0 or plan.ask != null) {
            plan.reply = try a.dupe(u8, "Done.");
            try warnings.append(a, try a.dupe(u8, "The model omitted its reply — the plan still ran"));
        } else {
            plan.reply = try a.dupe(u8, "The model returned an empty plan — nothing was changed.");
        }
    }
    plan.warnings = try warnings.toOwnedSlice(a);
}

/// The §1 note for a dropped variant: which one (its 1-based position, plus its label
/// when it has one) and why. Caller owns the result.
pub fn droppedVariantWarning(a: std.mem.Allocator, pos: usize, label: []const u8, op: []const u8) error{OutOfMemory}![]const u8 {
    const kind = if (opSchema.get().find(op).?.settings) "console-settings op" else "top-level-only op";
    if (label.len == 0)
        return std.fmt.allocPrint(a, "Dropped variant {d}: the {s} \"{s}\" can't run inside a variant — the rest of the plan ran", .{ pos, kind, op });
    return std.fmt.allocPrint(a, "Dropped variant {d} (\"{s}\"): the {s} \"{s}\" can't run inside a variant — the rest of the plan ran", .{ pos, label, kind, op });
}

// A plan carrying nothing but the given ACTIONS (or one VARIANT of them) — the envelope
// every op unit below needs, written once so the cases read as the op JSON they test.
const plan_head = "{\"reply\":\"ok\",\"actions\":[";
const variant_head = "{\"reply\":\"ok\",\"variants\":[{\"actions\":[";

pub fn planFor(comptime actions: []const u8) !Plan {
    return mustPlan(plan_head ++ actions ++ "]}");
}

pub fn rejectFor(comptime actions: []const u8, expected: []const u8) !void {
    return mustReject(plan_head ++ actions ++ "]}", expected);
}

pub fn planInVariant(comptime actions: []const u8) !Plan {
    return mustPlan(variant_head ++ actions ++ "]}]}");
}

pub fn rejectInVariant(comptime actions: []const u8, expected: []const u8) !void {
    return mustReject(variant_head ++ actions ++ "]}]}", expected);
}

test "parsePlan: chat-only fallback (no JSON object, or braces that are not JSON)" {
    var p1 = try mustPlan("  Just chatting, no ops needed.  ");
    defer p1.deinit();
    try testing.expect(p1.chat_only);
    try testing.expectEqualStrings("Just chatting, no ops needed.", p1.reply);
    try testing.expectEqual(@as(usize, 0), p1.actions.len);
    try testing.expectEqual(@as(usize, 0), p1.variants.len);

    var p2 = try mustPlan("some text with {braces that are not json");
    defer p2.deinit();
    try testing.expect(p2.chat_only);

    var p3 = try mustPlan("{ not: json }");
    defer p3.deinit();
    try testing.expect(p3.chat_only);
}

test "parsePlan: fence stripping + first balanced object + surrounding prose" {
    var p = try mustPlan(
        \\Sure! Here is the plan:
        \\```json
        \\{"version":1,"reply":"rotating","actions":[{"op":"rotate","dir":"left"}]}
        \\```
        \\Anything else?
    );
    defer p.deinit();
    try testing.expect(!p.chat_only);
    try testing.expectEqualStrings("rotating", p.reply);
    try testing.expectEqual(@as(usize, 1), p.actions.len);
    try testing.expect(p.actions[0].rotate.dir == .left);
    try testing.expectEqual(@as(u8, 1), p.actions[0].rotate.times); // default

    // Braces inside strings don't confuse the balanced-object scanner.
    var p2 = try mustPlan("{\"version\":2,\"reply\":\"ok {see}\",\"actions\":[]}");
    defer p2.deinit();
    try testing.expectEqualStrings("ok {see}", p2.reply); // version 2 accepted but ignored
}

test "parsePlan: unknown op drops with a warning; known op with bad params rejects" {
    var p = try planFor("{\"op\":\"resize\",\"w\":100},{\"op\":\"filter\",\"mode\":\"bw\"}");
    defer p.deinit();
    try testing.expectEqual(@as(usize, 1), p.actions.len); // resize dropped, filter kept
    try testing.expect(p.actions[0].filter.mode == .bw);
    try testing.expectEqual(@as(usize, 1), p.warnings.len);
    try testing.expectEqualStrings("Skipped unknown operation \"resize\"", p.warnings[0]);

    try rejectFor("{\"op\":\"rotate\",\"dir\":\"left\",\"times\":5}", "invalid rotate action: \"times\" must be an integer 1..3");
    try rejectFor("{\"op\":\"rotate\",\"dir\":\"up\"}", "invalid rotate action: \"dir\" must be one of \"left\", \"right\"");
    // Unknown fields on a KNOWN op fail the plan (strict validation).
    try rejectFor("{\"op\":\"rotate\",\"dir\":\"left\",\"angle\":45}", "invalid rotate action: unknown field \"angle\"");
    // §1 reply tolerance: "Done." + a warning, the plan itself survives.
    {
        var tolerated = try mustPlan("{\"actions\":[{\"op\":\"rotate\",\"dir\":\"left\"}]}");
        defer tolerated.deinit();
        try testing.expectEqualStrings("Done.", tolerated.reply);
        try testing.expectEqual(@as(usize, 1), tolerated.actions.len);
        try testing.expectEqual(@as(usize, 1), tolerated.warnings.len);
        try testing.expectEqualStrings("The model omitted its reply — the plan still ran", tolerated.warnings[0]);
    }
    {
        // An EMPTY plan says so — a bare "Done." would read as a success that
        // never occurred (contract §1).
        var blank_reply = try mustPlan("{\"reply\":\"  \"}");
        defer blank_reply.deinit();
        try testing.expectEqualStrings("The model returned an empty plan — nothing was changed.", blank_reply.reply);
        try testing.expectEqual(@as(usize, 0), blank_reply.warnings.len);
    }
    try mustReject("{\"reply\":\"ok\",\"actions\":{}}", "invalid plan: \"actions\" must be an array");
}

test "parsePlan: crop spec validation" {
    var p = try planFor("{\"op\":\"crop\",\"spec\":{\"x1\":\"10%\",\"x2\":\"-1.5cm\",\"y1\":\"0\",\"y2\":\"90px\"}}");
    defer p.deinit();
    try testing.expectEqualStrings("10%", p.actions[0].crop.x1.?);
    try testing.expectEqualStrings("-1.5cm", p.actions[0].crop.x2.?);

    try rejectFor("{\"op\":\"crop\",\"spec\":{}}", "invalid crop action: needs at least one of x1/x2/y1/y2/aspect/album");
    try rejectFor("{\"op\":\"crop\",\"spec\":{\"left\":\"10%\"}}", "invalid crop action: unknown field \"left\" in spec");
    try rejectFor("{\"op\":\"crop\",\"spec\":{\"x1\":\"10q\"}}", "invalid crop action: \"x1\" in spec must be a crop token (number with optional % / px / cm / in, leading \"-\" from the far side)");
    try rejectFor("{\"op\":\"crop\",\"spec\":{\"x1\":12}}", "invalid crop action: \"x1\" in spec must be a string");
}

test "parsePlan: crop aspect — strict W:H, digits only, both positive" {
    var p = try planFor("{\"op\":\"crop\",\"spec\":{\"x1\":\"10%\",\"aspect\":\"4:3\"}}");
    defer p.deinit();
    try testing.expectEqualStrings("10%", p.actions[0].crop.x1.?);
    try testing.expectEqualStrings("4:3", p.actions[0].crop.aspect.?);

    // Aspect alone satisfies the at-least-one-key rule.
    var alone = try planFor("{\"op\":\"crop\",\"spec\":{\"aspect\":\"16:9\"}}");
    defer alone.deinit();
    try testing.expectEqualStrings("16:9", alone.actions[0].crop.aspect.?);
    try testing.expect(alone.actions[0].crop.x1 == null);

    // Malformed aspect = invalid params on a known op = the whole plan fails.
    inline for (.{ "0:3", "4:0", "-1:2", "4:-3", "3:4:5", "a:b", "1.5:2", "4", "4:", ":3", "1e2:3", "" }) |aspect| {
        try mustReject(
            "{\"reply\":\"ok\",\"actions\":[{\"op\":\"crop\",\"spec\":{\"aspect\":\"" ++ aspect ++ "\"}}]}",
            "invalid crop action: \"aspect\" in spec must be a \"W:H\" aspect with both sides > 0",
        );
    }
    try rejectFor("{\"op\":\"crop\",\"spec\":{\"aspect\":43}}", "invalid crop action: \"aspect\" in spec must be a string");
}

test "parsePlan: action-level crop aspect folds into the spec; a conflicting duplicate fails" {
    // The tolerated spelling: "aspect" BESIDE "spec" — folded in, same validation.
    var beside = try planFor("{\"op\":\"crop\",\"spec\":{\"x1\":\"10%\"},\"aspect\":\"3:4\"}");
    defer beside.deinit();
    try testing.expectEqualStrings("10%", beside.actions[0].crop.x1.?);
    try testing.expectEqualStrings("3:4", beside.actions[0].crop.aspect.?);

    // Beside an EMPTY spec it still satisfies the at-least-one-key rule.
    var alone = try planFor("{\"op\":\"crop\",\"spec\":{},\"aspect\":\"16:9\"}");
    defer alone.deinit();
    try testing.expectEqualStrings("16:9", alone.actions[0].crop.aspect.?);

    // An EQUAL duplicate folds silently; the spec's own value wins the fold.
    var equal = try planFor("{\"op\":\"crop\",\"spec\":{\"aspect\":\"1:1\"},\"aspect\":\"1:1\"}");
    defer equal.deinit();
    try testing.expectEqualStrings("1:1", equal.actions[0].crop.aspect.?);

    // Conflicting duplicates = invalid params on a known op = the whole plan fails.
    try rejectFor("{\"op\":\"crop\",\"spec\":{\"aspect\":\"3:4\"},\"aspect\":\"4:3\"}", "invalid crop action: \"aspect\" appears both beside \"spec\" and inside it with different values");
    // The action-level spelling gets the same W:H validation …
    try rejectFor("{\"op\":\"crop\",\"spec\":{\"x1\":\"10%\"},\"aspect\":\"0:3\"}", "invalid crop action: \"aspect\" in spec must be a \"W:H\" aspect with both sides > 0");
    try rejectFor("{\"op\":\"crop\",\"spec\":{\"x1\":\"10%\"},\"aspect\":43}", "invalid crop action: \"aspect\" in spec must be a string");
    // … and other stray fields on crop still fail the plan.
    try rejectFor("{\"op\":\"crop\",\"spec\":{\"x1\":\"10%\"},\"ratio\":\"3:4\"}", "invalid crop action: unknown field \"ratio\"");
}

test "parsePlan: filter tint rules, formula charset, page + blank formats, frame exclusivity" {
    var p = try planFor("{\"op\":\"filter\",\"mode\":\"custom\",\"tint\":\"#A1b2c3\"},{\"op\":\"formula\",\"axis\":\"y\",\"expr\":\"y*2 + 1\"},{\"op\":\"page\",\"format\":\"c10\"},{\"op\":\"blank\",\"color\":\"#ffffff\",\"format\":\"a4\"}");
    defer p.deinit();
    try testing.expectEqualStrings("#A1b2c3", p.actions[0].filter.tint);
    try testing.expectEqual(@as(u8, 'y'), p.actions[1].formula.axis);
    try testing.expectEqualStrings("c10", p.actions[2].page.format);
    try testing.expectEqualStrings("a4", p.actions[3].blank.format.?);

    try rejectFor("{\"op\":\"filter\",\"mode\":\"custom\"}", "invalid filter action: \"tint\" is required with \"mode\" \"custom\"");
    try rejectFor("{\"op\":\"filter\",\"mode\":\"bw\",\"tint\":\"#112233\"}", "invalid filter action: \"tint\" only applies with \"mode\" \"custom\"");
    try rejectFor("{\"op\":\"formula\",\"axis\":\"x\",\"expr\":\"y*2\"}", "invalid formula action: \"expr\" must be digits, + - * / ( ) . and \"x\"");
    try mustReject(
        "{\"reply\":\"ok\",\"actions\":[{\"op\":\"page\",\"format\":\"A4\"}]}", // lowercase only
        "invalid page action: \"format\" must be a lowercase ISO name a0\u{2013}a10, b0\u{2013}b10 or c0\u{2013}c10",
    );
    try rejectFor("{\"op\":\"blank\",\"color\":\"#12345\"}", "invalid blank action: \"color\" must be #rrggbb or a CSS color name");
    try rejectFor("{\"op\":\"frame\",\"index\":0,\"indices\":[1]}", "invalid frame action: exactly one of \"index\" / \"indices\" is required");
    try rejectFor("{\"op\":\"frame\",\"indices\":[]}", "invalid frame action: \"indices\" must be a non-empty array");

    var pf = try planFor("{\"op\":\"frame\",\"index\":3}");
    defer pf.deinit();
    try testing.expectEqualSlices(u32, &.{3}, pf.actions[0].frame.indices);
    try testing.expect(pf.hasFrameOp());
}

test "parsePlan: layout line validation + re-serialization" {
    var p = try mustPlan(
        \\{"reply":"drawn","actions":[{"op":"layout","lines":[
        \\  {"points":[{"x":1,"y":2},{"x":3,"y":4}],"color":"#FF0000","style":"dashed","locked":true}
        \\]}]}
    );
    defer p.deinit();
    // The validated lines round-trip as a JSON array the /apply path consumes directly.
    try testing.expectEqualStrings(
        "[{\"points\":[{\"x\":1,\"y\":2},{\"x\":3,\"y\":4}],\"color\":\"#FF0000\",\"style\":\"dashed\",\"locked\":true}]",
        p.actions[0].layout.lines_json,
    );

    try rejectFor("{\"op\":\"layout\",\"lines\":[{\"points\":[{\"x\":1,\"y\":2}],\"style\":\"wavy\"}]}", "invalid layout action: \"style\" in lines[0] must be one of \"solid\", \"dashed\", \"dotted\"");
    try rejectFor("{\"op\":\"layout\",\"lines\":[{\"points\":[{\"x\":1,\"z\":2}]}]}", "invalid layout action: unknown field \"z\" in lines[0].points[0]");
    try rejectFor("{\"op\":\"layout\",\"lines\":[{\"points\":[{\"x\":1,\"y\":2}],\"glow\":true}]}", "invalid layout action: unknown field \"glow\" in lines[0]");
}

test "parsePlan: limits (16 actions, 8 variants, 200 lines, 5000-char strings)" {
    const a = testing.allocator;

    // 17 actions → rejected.
    var many: std.ArrayList(u8) = .empty;
    defer many.deinit(a);
    try many.appendSlice(a, "{\"reply\":\"ok\",\"actions\":[");
    for (0..17) |i| {
        if (i != 0) try many.append(a, ',');
        try many.appendSlice(a, "{\"op\":\"rotate\",\"dir\":\"left\"}");
    }
    try many.appendSlice(a, "]}");
    try mustReject(many.items, "invalid plan: more than 16 entries in \"actions\"");

    // 9 variants → rejected.
    var vars: std.ArrayList(u8) = .empty;
    defer vars.deinit(a);
    try vars.appendSlice(a, "{\"reply\":\"ok\",\"variants\":[");
    for (0..9) |i| {
        if (i != 0) try vars.append(a, ',');
        try vars.appendSlice(a, "{\"label\":\"v\",\"actions\":[]}");
    }
    try vars.appendSlice(a, "]}");
    try mustReject(vars.items, "invalid plan: more than 8 entries in \"variants\"");

    // 201 layout lines → rejected.
    var lines: std.ArrayList(u8) = .empty;
    defer lines.deinit(a);
    try lines.appendSlice(a, "{\"reply\":\"ok\",\"actions\":[{\"op\":\"layout\",\"lines\":[");
    for (0..201) |i| {
        if (i != 0) try lines.append(a, ',');
        try lines.appendSlice(a, "{\"points\":[{\"x\":0,\"y\":0}]}");
    }
    try lines.appendSlice(a, "]}]}");
    try mustReject(lines.items, "invalid layout action: more than 200 entries in \"lines\"");

    // A formula expression over 5000 chars → rejected (per-string cap).
    var expr: std.ArrayList(u8) = .empty;
    defer expr.deinit(a);
    try expr.appendSlice(a, "{\"reply\":\"ok\",\"actions\":[{\"op\":\"formula\",\"axis\":\"x\",\"expr\":\"");
    for (0..5001) |_| try expr.append(a, 'x');
    try expr.appendSlice(a, "\"}]}");
    try mustReject(expr.items, "invalid formula action: \"expr\" is longer than 5000 characters");
}

test "parsePlan: variants (labels, defaults, per-variant actions + frame detection)" {
    var p = try mustPlan(
        \\{"reply":"4 takes","actions":[],"variants":[
        \\  {"label":"Rotated!","actions":[{"op":"rotate","dir":"right","times":2}]},
        \\  {"actions":[{"op":"filter","mode":"sepia"}]}
        \\]}
    );
    defer p.deinit();
    try testing.expectEqual(@as(usize, 2), p.variants.len);
    try testing.expectEqualStrings("Rotated!", p.variants[0].label);
    try testing.expectEqualStrings("", p.variants[1].label); // empty → positional file name
    try testing.expectEqual(@as(u8, 2), p.variants[0].actions[0].rotate.times);
    try testing.expect(!p.hasFrameOp());

    var pf = try planInVariant("{\"op\":\"frame\",\"index\":1}");
    defer pf.deinit();
    try testing.expect(pf.hasFrameOp()); // frame inside a variant is still a plan-level error

    try mustReject(
        "{\"reply\":\"ok\",\"variants\":[{\"label\":7}]}",
        "invalid plan: \"label\" in variants[0] must be a string",
    );
    try rejectInVariant("{\"op\":\"rotate\"}", "invalid rotate action: \"dir\" is required");
    // The envelope tolerates undeclared keys on a variant object (allowUnknown).
    var extra = try mustPlan("{\"reply\":\"ok\",\"variants\":[{\"label\":\"v\",\"actions\":[],\"note\":\"x\"}]}");
    defer extra.deinit();
    try testing.expectEqual(@as(usize, 1), extra.variants.len);
}

test "parsePlan: §2.1 image/save shapes, and both are top-level only" {
    var p = try mustPlan(
        \\{"reply":"three","actions":[{"op":"image","index":2},
        \\  {"op":"save","name":"portrait 1"},{"op":"save"}]}
    );
    defer p.deinit();
    try testing.expectEqual(@as(usize, 3), p.actions.len);
    try testing.expectEqual(@as(u32, 2), p.actions[0].image.index);
    try testing.expectEqualStrings("portrait 1", p.actions[1].save.name);
    try testing.expectEqualStrings("", p.actions[2].save.name); // derived at execution

    // index is 1-based: 0, negatives, non-integers and strings are not an attachment.
    const bad_index = [_][]const u8{ "0", "-1", "1.5", "\"1\"", "null", "true" };
    const bad_msg = [_][]const u8{
        "invalid image action: \"index\" must be an integer >= 1",
        "invalid image action: \"index\" must be an integer >= 1",
        "invalid image action: \"index\" must be an integer",
        "invalid image action: \"index\" must be an integer",
        "invalid image action: \"index\" is required",
        "invalid image action: \"index\" must be an integer",
    };
    for (bad_index, bad_msg) |v, msg| {
        const raw = try std.fmt.allocPrint(testing.allocator, "{{\"reply\":\"ok\",\"actions\":[{{\"op\":\"image\",\"index\":{s}}}]}}", .{v});
        defer testing.allocator.free(raw);
        try mustReject(raw, msg);
    }
    try rejectFor("{\"op\":\"image\",\"index\":1,\"name\":\"x\"}", "invalid image action: unknown field \"name\"");

    // A save name is bounded (§2.1) and must be a string; unknown fields fail the plan.
    {
        const long = try std.fmt.allocPrint(testing.allocator, "{{\"reply\":\"ok\",\"actions\":[{{\"op\":\"save\",\"name\":\"{s}\"}}]}}", .{"x" ** 121});
        defer testing.allocator.free(long);
        try mustReject(long, "invalid save action: \"name\" is longer than 120 characters");
    }
    try rejectFor("{\"op\":\"save\",\"name\":7}", "invalid save action: \"name\" must be a string");
    // §10: a save may carry a destination — a folder or a file name — which the executor
    // accepts only if the USER wrote it. A URL is never a destination (openUrl owns those).
    {
        var dest = try planFor("{\"op\":\"save\",\"path\":\"~/Downloads\"}");
        defer dest.deinit();
        try testing.expectEqualStrings("~/Downloads", dest.actions[0].save.path);
    }
    try rejectFor("{\"op\":\"save\",\"path\":\"https://x.example/out.png\"}", "invalid save action: \"path\" must be a local value, not a URL");

    // …and neither may hide inside a variant (which renders ONE image's alternatives):
    // that variant is dropped with a note, never the whole plan (§1/§2.1).
    inline for (.{ "{\"op\":\"image\",\"index\":1}", "{\"op\":\"save\"}" }, .{ "image", "save" }) |op_json, name| {
        var dropped = try mustPlan("{\"reply\":\"ok\",\"variants\":[{\"actions\":[" ++ op_json ++ "]}]}");
        defer dropped.deinit();
        try testing.expectEqual(@as(usize, 0), dropped.variants.len);
        try testing.expectEqual(@as(usize, 1), dropped.warnings.len);
        try testing.expectEqualStrings(
            "Dropped variant 1: the top-level-only op \"" ++ name ++ "\" can't run inside a variant — the rest of the plan ran",
            dropped.warnings[0],
        );
    }
}

test "parsePlan: console ops validate (accent hex, connect/disconnect server, delete path)" {
    var p = try mustPlan(
        \\{"reply":"done","actions":[{"op":"accent","color":"#00FFff"},
        \\  {"op":"connect","server":"http://a:8090"},{"op":"disconnect","server":" a "},
        \\  {"op":"delete","path":"old.stencil"}]}
    );
    defer p.deinit();
    try testing.expectEqual(@as(usize, 4), p.actions.len);
    try testing.expectEqualStrings("#00FFff", p.actions[0].accent.color);
    try testing.expectEqualStrings("http://a:8090", p.actions[1].connect.server);
    try testing.expectEqualStrings("a", p.actions[2].disconnect.server); // trimmed
    try testing.expectEqualStrings("old.stencil", p.actions[3].delete.path);
    // None of the console ops trips the frame/image plan-level checks or a continuation.
    try testing.expect(!p.hasFrameOp() and !p.loadsWithoutTracing());
}

test "parsePlan: console ops with bad params reject the whole plan" {
    // accent takes exactly a #rrggbb hex — names and shorthand hexes are the model's to translate.
    try rejectFor("{\"op\":\"accent\",\"color\":\"cyan\"}", "invalid accent action: \"color\" must be #rrggbb");
    try rejectFor("{\"op\":\"accent\",\"color\":\"#0ff\"}", "invalid accent action: \"color\" must be #rrggbb");
    try rejectFor("{\"op\":\"accent\"}", "invalid accent action: exactly one of \"color\" / \"preset\" is required");
    try rejectFor("{\"op\":\"accent\",\"color\":\"#00ffff\",\"mode\":\"dark\"}", "invalid accent action: unknown field \"mode\"");
    // connect/disconnect need a non-empty server; delete a non-empty path.
    try rejectFor("{\"op\":\"connect\",\"server\":\"\"}", "invalid connect action: \"server\" must be a non-empty string");
    try rejectFor("{\"op\":\"connect\"}", "invalid connect action: \"server\" is required");
    try rejectFor("{\"op\":\"disconnect\",\"server\":3}", "invalid disconnect action: \"server\" must be a string");
    try rejectFor("{\"op\":\"connect\",\"server\":\"a\",\"token\":\"t\"}", "invalid connect action: unknown field \"token\"");
    try rejectFor("{\"op\":\"delete\",\"path\":\" \"}", "invalid delete action: \"path\" must be a non-empty string");
    try rejectFor("{\"op\":\"delete\",\"name\":\"x\"}", "invalid delete action: unknown field \"name\"");
}

test "parsePlan: console ops are top-level only — a variant carrying one is dropped" {
    const inside = [_][]const u8{
        "{\"op\":\"accent\",\"color\":\"#00ffff\"}",
        "{\"op\":\"connect\",\"server\":\"a\"}",
        "{\"op\":\"disconnect\",\"server\":\"a\"}",
        "{\"op\":\"reconnect\",\"server\":\"a\"}",
        "{\"op\":\"delete\",\"path\":\"x.stencil\"}",
        "{\"op\":\"openUrl\",\"url\":\"https://a.example/x.png\"}",
        "{\"op\":\"copy\"}",
        "{\"op\":\"clear\"}",
    };
    const names = [_][]const u8{ "accent", "connect", "disconnect", "reconnect", "delete", "openUrl", "copy", "clear" };
    for (inside, names) |op_json, name| {
        const raw = try std.fmt.allocPrint(testing.allocator, "{{\"reply\":\"x\",\"variants\":[{{\"actions\":[{s}]}}]}}", .{op_json});
        defer testing.allocator.free(raw);
        var p = try mustPlan(raw);
        defer p.deinit();
        const expected = try std.fmt.allocPrint(
            testing.allocator,
            "Dropped variant 1: the console-settings op \"{s}\" can't run inside a variant — the rest of the plan ran",
            .{name},
        );
        defer testing.allocator.free(expected);
        try testing.expectEqual(@as(usize, 0), p.variants.len);
        try testing.expectEqual(@as(usize, 1), p.warnings.len);
        try testing.expectEqualStrings(expected, p.warnings[0]);
    }
    // §2's undo/redo/reset are top-level only too — a distinct (§2.1-style) reason.
    inline for (.{ "undo", "redo", "reset" }) |op| {
        var p = try mustPlan("{\"reply\":\"x\",\"variants\":[{\"actions\":[{\"op\":\"" ++ op ++ "\"}]}]}");
        defer p.deinit();
        try testing.expectEqual(@as(usize, 0), p.variants.len);
        try testing.expectEqualStrings(
            "Dropped variant 1: the top-level-only op \"" ++ op ++ "\" can't run inside a variant — the rest of the plan ran",
            p.warnings[0],
        );
    }
}

test "parsePlan: a misplaced op costs its variant, not the plan (contract §1)" {
    // Good top-level actions + a bad variant + a good one: everything else survives,
    // and the note names the dropped variant by position AND label.
    var p = try mustPlan(
        \\{"reply":"two takes","actions":[{"op":"rotate","dir":"right"}],"variants":[
        \\  {"label":"Wiped","actions":[{"op":"filter","mode":"bw"},{"op":"clear"}]},
        \\  {"label":"Sepia","actions":[{"op":"filter","mode":"sepia"}]}
        \\]}
    );
    defer p.deinit();
    try testing.expectEqual(@as(usize, 1), p.actions.len);
    try testing.expect(p.actions[0] == .rotate);
    try testing.expectEqual(@as(usize, 1), p.variants.len);
    try testing.expectEqualStrings("Sepia", p.variants[0].label);
    try testing.expectEqual(@as(usize, 1), p.warnings.len);
    try testing.expectEqualStrings(
        "Dropped variant 1 (\"Wiped\"): the console-settings op \"clear\" can't run inside a variant — the rest of the plan ran",
        p.warnings[0],
    );

    // A plan whose ONLY content was such a variant is still a normal reply + note.
    var only = try mustPlan("{\"reply\":\"cleared\",\"variants\":[{\"actions\":[{\"op\":\"clearChat\"}]}]}");
    defer only.deinit();
    try testing.expectEqualStrings("cleared", only.reply);
    try testing.expectEqual(@as(usize, 0), only.actions.len);
    try testing.expectEqual(@as(usize, 0), only.variants.len);
    try testing.expectEqualStrings(
        "Dropped variant 1: the console-settings op \"clearChat\" can't run inside a variant — the rest of the plan ran",
        only.warnings[0],
    );

    // The other strictness is untouched: inside a variant a known op with bad params still fails the
    // plan, an unknown one is still skipped, and a forbidden name is still rejected outright (§13).
    try rejectInVariant("{\"op\":\"rotate\",\"dir\":\"up\"}", "invalid rotate action: \"dir\" must be one of \"left\", \"right\"");
    try rejectInVariant("{\"op\":\"chatPersist\",\"on\":true}", "invalid plan: the \"chatPersist\" op is never model-drivable");
    var unknown = try planInVariant("{\"op\":\"resize\",\"w\":2},{\"op\":\"filter\",\"mode\":\"bw\"}");
    defer unknown.deinit();
    try testing.expectEqual(@as(usize, 1), unknown.variants.len);
    try testing.expectEqualStrings("Skipped unknown operation \"resize\"", unknown.warnings[0]);
}

test "parsePlan: §2 undo/redo/reset shapes — steps 1..20, default 1, reset fieldless" {
    var p = try mustPlan(
        \\{"reply":"back","actions":[{"op":"undo"},{"op":"redo","steps":3},
        \\  {"op":"undo","steps":20},{"op":"reset"}]}
    );
    defer p.deinit();
    try testing.expectEqual(@as(usize, 4), p.actions.len);
    try testing.expectEqual(@as(u8, 1), p.actions[0].undo.steps); // default
    try testing.expectEqual(@as(u8, 3), p.actions[1].redo.steps);
    try testing.expectEqual(@as(u8, 20), p.actions[2].undo.steps);
    try testing.expect(p.actions[3] == .reset);
    // None of them trips the plan-level frame/image checks or a §7 continuation.
    try testing.expect(!p.hasFrameOp() and !p.loadsWithoutTracing());

    // steps out of range / non-integer, and stray fields, fail the plan.
    try rejectFor("{\"op\":\"undo\",\"steps\":0}", "invalid undo action: \"steps\" must be an integer 1..20");
    try rejectFor("{\"op\":\"redo\",\"steps\":21}", "invalid redo action: \"steps\" must be an integer 1..20");
    try rejectFor("{\"op\":\"undo\",\"steps\":\"2\"}", "invalid undo action: \"steps\" must be an integer");
    try rejectFor("{\"op\":\"undo\",\"all\":true}", "invalid undo action: unknown field \"all\"");
    try rejectFor("{\"op\":\"reset\",\"steps\":1}", "invalid reset action: unknown field \"steps\"");
}

test "parsePlan: formula enabled-form and empty-expr clears (§2, exactly one form)" {
    var p = try mustPlan(
        \\{"reply":"off","actions":[{"op":"formula","enabled":false},
        \\  {"op":"formula","enabled":true},{"op":"formula","axis":"x","expr":""},
        \\  {"op":"formula","axis":"y","expr":"  "}]}
    );
    defer p.deinit();
    try testing.expectEqual(@as(?bool, false), p.actions[0].formula.enabled);
    try testing.expectEqual(@as(?bool, true), p.actions[1].formula.enabled);
    try testing.expectEqual(@as(?bool, null), p.actions[2].formula.enabled);
    try testing.expectEqual(@as(u8, 'x'), p.actions[2].formula.axis);
    try testing.expectEqualStrings("", p.actions[2].formula.expr); // empty clears that axis
    try testing.expectEqualStrings("", p.actions[3].formula.expr); // blank ≡ empty

    // `enabled` rides ALONE; it must be a bool; the axis form still needs its axis.
    try rejectFor("{\"op\":\"formula\",\"enabled\":false,\"axis\":\"x\"}", "invalid formula action: exactly one of \"enabled\" / \"axis\"+\"expr\" is required");
    try rejectFor("{\"op\":\"formula\",\"enabled\":\"off\"}", "invalid formula action: \"enabled\" must be a boolean");
    try rejectFor("{\"op\":\"formula\",\"expr\":\"x*2\"}", "invalid formula action: exactly one of \"enabled\" / \"axis\"+\"expr\" is required");
    try rejectFor("{\"op\":\"formula\",\"axis\":\"x\",\"expr\":7}", "invalid formula action: \"expr\" must be a string");
}

test "parsePlan: page custom cm dims (§2, exactly one of format / width+height)" {
    var p = try planFor("{\"op\":\"page\",\"width\":21,\"height\":29.7}");
    defer p.deinit();
    try testing.expectEqualStrings("", p.actions[0].page.format); // "" = the custom form
    try testing.expectEqual(@as(f64, 21), p.actions[0].page.width);
    try testing.expectEqual(@as(f64, 29.7), p.actions[0].page.height);

    try rejectFor("{\"op\":\"page\",\"format\":\"a4\",\"width\":21,\"height\":29.7}", "invalid page action: exactly one of \"format\" / \"width\"+\"height\" is required");
    try rejectFor("{\"op\":\"page\",\"width\":21}", "invalid page action: exactly one of \"format\" / \"width\"+\"height\" is required");
    try rejectFor("{\"op\":\"page\",\"width\":0.05,\"height\":10}", "invalid page action: \"width\" must be a number 0.1..500");
    try rejectFor("{\"op\":\"page\",\"width\":10,\"height\":501}", "invalid page action: \"height\" must be a number 0.1..500");
    try rejectFor("{\"op\":\"page\",\"width\":\"21\",\"height\":10}", "invalid page action: \"width\" must be a number");
    // A bare page is neither form.
    try rejectFor("{\"op\":\"page\"}", "invalid page action: exactly one of \"format\" / \"width\"+\"height\" is required");
}

test "parsePlan: blank cm dims (§2 — both or neither, overriding format)" {
    var p = try mustPlan(
        \\{"reply":"ok","actions":[{"op":"blank","color":"#ffffff","width":10,"height":5},
        \\  {"op":"blank","color":"pink","format":"a4","width":10,"height":5}]}
    );
    defer p.deinit();
    try testing.expectEqual(@as(f64, 10), p.actions[0].blank.width);
    try testing.expectEqual(@as(f64, 5), p.actions[0].blank.height);
    try testing.expect(p.actions[0].blank.format == null);
    // Dims may ride beside a format — execution lets them override it (§2).
    try testing.expectEqualStrings("a4", p.actions[1].blank.format.?);
    try testing.expectEqual(@as(f64, 10), p.actions[1].blank.width);

    try rejectFor("{\"op\":\"blank\",\"color\":\"#ffffff\",\"width\":10}", "invalid blank action: \"width\" and \"height\" ride together");
    try rejectFor("{\"op\":\"blank\",\"color\":\"#ffffff\",\"width\":10,\"height\":501}", "invalid blank action: \"height\" must be a number 0.1..500");
    try rejectFor("{\"op\":\"blank\",\"color\":\"#ffffff\",\"width\":true,\"height\":5}", "invalid blank action: \"width\" must be a number");
}

test "parsePlan: crop album spec key (console profile) — a bool riding the spec" {
    var p = try planFor("{\"op\":\"crop\",\"spec\":{\"x1\":\"10%\",\"album\":true}},{\"op\":\"crop\",\"spec\":{\"y2\":\"90%\",\"album\":false}}");
    defer p.deinit();
    try testing.expect(p.actions[0].crop.album);
    try testing.expectEqualStrings("10%", p.actions[0].crop.x1.?);
    try testing.expect(!p.actions[1].crop.album);

    // album is a modifier, not an edge: alone it does not satisfy the at-least-one rule.
    try rejectFor("{\"op\":\"crop\",\"spec\":{\"album\":true}}", "invalid crop action: spec needs at least one of x1/x2/y1/y2/aspect");
    try rejectFor("{\"op\":\"crop\",\"spec\":{\"x1\":\"10%\",\"album\":\"yes\"}}", "invalid crop action: \"album\" in spec must be a boolean");
}

test "parsePlan: accent preset form (§10 — exactly one of color / preset)" {
    var p = try planFor("{\"op\":\"accent\",\"preset\":\" green \"},{\"op\":\"accent\",\"color\":\"#00ffff\"}");
    defer p.deinit();
    try testing.expectEqualStrings("green", p.actions[0].accent.preset); // trimmed
    try testing.expectEqualStrings("", p.actions[0].accent.color);
    try testing.expectEqualStrings("#00ffff", p.actions[1].accent.color);
    try testing.expectEqualStrings("", p.actions[1].accent.preset);

    try rejectFor("{\"op\":\"accent\",\"color\":\"#00ffff\",\"preset\":\"green\"}", "invalid accent action: exactly one of \"color\" / \"preset\" is required");
    try rejectFor("{\"op\":\"accent\",\"preset\":\"  \"}", "invalid accent action: \"preset\" must be a non-empty string");
    try rejectFor("{\"op\":\"accent\",\"preset\":7}", "invalid accent action: \"preset\" must be a string");
}

test "parsePlan: reconnect and clear shapes (console profile §10)" {
    var p = try planFor("{\"op\":\"reconnect\",\"server\":\" a \"},{\"op\":\"clear\"}");
    defer p.deinit();
    try testing.expectEqualStrings("a", p.actions[0].reconnect.server); // trimmed
    try testing.expect(p.actions[1] == .clear);
    try testing.expect(!p.loadsWithoutTracing()); // a clear is a removal, not a load

    try rejectFor("{\"op\":\"reconnect\",\"server\":\"\"}", "invalid reconnect action: \"server\" must be a non-empty string");
    try rejectFor("{\"op\":\"reconnect\"}", "invalid reconnect action: \"server\" is required");
    try rejectFor("{\"op\":\"reconnect\",\"server\":\"a\",\"token\":\"t\"}", "invalid reconnect action: unknown field \"token\"");
    try rejectFor("{\"op\":\"clear\",\"what\":\"image\"}", "invalid clear action: unknown field \"what\"");
}

test "parsePlan: clearChat — fieldless, top-level only (§10)" {
    var p = try planFor("{\"op\":\"clearChat\"}");
    defer p.deinit();
    try testing.expect(p.actions[0] == .clear_chat);
    try testing.expect(!p.loadsWithoutTracing()); // clearing chat loads nothing

    // Any field at all fails the plan; a variant carrying the op just loses the variant.
    try rejectFor("{\"op\":\"clearChat\",\"scope\":\"all\"}", "invalid clearChat action: unknown field \"scope\"");
    try rejectFor("{\"op\":\"clearChat\",\"confirm\":true}", "invalid clearChat action: unknown field \"confirm\"");
    // The persistence/consent toggles stay forbidden — never even unknown-op skips.
    try rejectFor("{\"op\":\"chatPersist\",\"on\":true}", "invalid plan: the \"chatPersist\" op is never model-drivable");
}

test "§13 forbidden ops: never registered, and a plan naming one is rejected outright" {
    const isForbiddenOp = registry.isForbiddenOp;
    for (op_registry) |d| try testing.expect(!isForbiddenOp(d.name));
    try testing.expect(isForbiddenOp("llm") and isForbiddenOp("paste") and isForbiddenOp("quit"));
    // Rejected with the §13 message — a hard tooth, never the unknown-op skip.
    try rejectFor("{\"op\":\"llm\",\"provider\":\"ollama\"}", "invalid plan: the \"llm\" op is never model-drivable");
    try rejectFor("{\"op\":\"paste\"}", "invalid plan: the \"paste\" op is never model-drivable");
    // …inside variants too.
    try rejectInVariant("{\"op\":\"exit\"}", "invalid plan: the \"exit\" op is never model-drivable");
}

test "parsePlan: openUrl takes an http(s) url + optional incognito; copy takes no fields (§10)" {
    var p = try mustPlan(
        \\{"reply":"ok","actions":[{"op":"openUrl","url":" https://a.example/cat.png ","incognito":true},
        \\  {"op":"openUrl","url":"HTTP://b.example/x.jpg"},{"op":"copy"}]}
    );
    defer p.deinit();
    try testing.expectEqual(@as(usize, 3), p.actions.len);
    try testing.expectEqualStrings("https://a.example/cat.png", p.actions[0].open_url.url); // trimmed
    try testing.expect(p.actions[0].open_url.incognito);
    try testing.expect(!p.actions[1].open_url.incognito); // absent = false
    try testing.expect(p.actions[2] == .copy);

    // Only http(s), no whitespace, url required; incognito must be a bool.
    try rejectFor("{\"op\":\"openUrl\",\"url\":\"ftp://a.example/x\"}", "invalid openUrl action: \"url\" must be an http(s) URL");
    try rejectFor("{\"op\":\"openUrl\",\"url\":\"https://a.example/a b\"}", "invalid openUrl action: \"url\" must be an http(s) URL");
    try rejectFor("{\"op\":\"openUrl\",\"url\":\"https://\"}", "invalid openUrl action: \"url\" must be an http(s) URL");
    try rejectFor("{\"op\":\"openUrl\"}", "invalid openUrl action: \"url\" is required");
    try rejectFor("{\"op\":\"openUrl\",\"url\":\"https://a.example/x\",\"incognito\":\"yes\"}", "invalid openUrl action: \"incognito\" must be a boolean");
    try rejectFor("{\"op\":\"openUrl\",\"url\":\"https://a.example/x\",\"tab\":1}", "invalid openUrl action: unknown field \"tab\"");
    // copy is fieldless — anything else on it fails the plan.
    try rejectFor("{\"op\":\"copy\",\"name\":\"out\"}", "invalid copy action: unknown field \"name\"");
}

test "parsePlan: the GUI editors' §10 ops stay unknown here — skipped with a warning" {
    var p = try mustPlan(
        \\{"reply":"ok","actions":[{"op":"theme","mode":"dark"},{"op":"units","value":"cm"},
        \\  {"op":"filter","mode":"bw"}]}
    );
    defer p.deinit();
    try testing.expectEqual(@as(usize, 1), p.actions.len); // only the filter survives
    try testing.expectEqual(@as(usize, 2), p.warnings.len);
    try testing.expectEqualStrings("Skipped unknown operation \"theme\"", p.warnings[0]);
    try testing.expectEqualStrings("Skipped unknown operation \"units\"", p.warnings[1]);
}

// The plan returned for `raw`, which the test expects to be VALID (not `.invalid`).
pub fn mustPlan(raw: []const u8) !Plan {
    switch (try parsePlan(testing.allocator, raw)) {
        .plan => |p| return p,
        .invalid => |msg| {
            defer testing.allocator.free(msg);
            std.debug.print("unexpectedly invalid: {s}\n", .{msg});
            return error.TestUnexpectedResult;
        },
    }
}

// The rejection message for `raw`, which the test expects to be INVALID.
pub fn mustReject(raw: []const u8, expected: []const u8) !void {
    switch (try parsePlan(testing.allocator, raw)) {
        .plan => |p| {
            var plan = p;
            defer plan.deinit();
            std.debug.print("unexpectedly valid for: {s}\n", .{raw});
            return error.TestUnexpectedResult;
        },
        .invalid => |msg| {
            defer testing.allocator.free(msg);
            try testing.expectEqualStrings(expected, msg);
        },
    }
}

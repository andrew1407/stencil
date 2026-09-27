//! A model reply → a validated `Plan` (contract §3). core/opplan walks the reply (opSchema.parse)
//! and this maps core's one result document onto the typed plan, every failure and warning in
//! core's canonical §1 words. Anything core cannot prove is a rejection; a misplaced op costs its variant.
const std = @import("std");
const opSchema = @import("../opSchema.zig");
const ObjectMap = opSchema.ObjectMap;
const Value = opSchema.Value;
const testing = std.testing;
const model = @import("model.zig");
const normalize = @import("normalize.zig");
const askCard = @import("ask.zig").card;
const Plan = model.Plan;
const Variant = model.Variant;
const Action = model.Action;

/// parsePlan's outcome: a validated plan, or a user-facing rejection message (gpa-owned).
pub const ParseOutcome = union(enum) {
    plan: Plan,
    invalid: []u8,
};

/// Parse the raw LLM reply into a validated plan (§1): core strips fences, takes the first balanced
/// `{…}` (none → chat-only turn), drops unknown ops, rejects bad params and drops a misplaced variant.
pub fn parsePlan(gpa: std.mem.Allocator, raw: []const u8) error{OutOfMemory}!ParseOutcome {
    var plan = Plan{ .arena = std.heap.ArenaAllocator.init(gpa) };
    errdefer plan.arena.deinit();
    const a = plan.arena.allocator();
    const result = try opSchema.parse(a, raw);
    const doc = result.doc;
    plan.reply = doc.get("reply").?.string;
    switch (result.status) {
        .invalid => {
            const msg = try gpa.dupe(u8, doc.get("error").?.object.get("message").?.string);
            plan.arena.deinit();
            return .{ .invalid = msg };
        },
        .chat_only => plan.chat_only = true,
        .valid => {
            plan.actions = try typedActions(a, doc.get("actions").?);
            var variants: std.ArrayList(Variant) = .empty;
            for (doc.get("variants").?.array.items) |v| {
                const label = v.object.get("label").?;
                // An absent label stays empty — file naming falls back to the variant's position.
                try variants.append(a, .{ .label = if (label == .string) label.string else "", .actions = try typedActions(a, v.object.get("actions").?) });
            }
            plan.variants = try variants.toOwnedSlice(a);
            const ask = doc.get("ask").?;
            plan.ask = if (ask == .object) try askCard(a, ask.object) else null;
            plan.warnings = try warnings(a, doc);
        },
    }
    return .{ .plan = plan };
}

fn typedActions(a: std.mem.Allocator, list: Value) error{OutOfMemory}![]Action {
    var out: std.ArrayList(Action) = .empty;
    for (list.array.items) |n| try out.append(a, try normalize.fill(a, n.object.get("op").?.string, n.object));
    return out.toOwnedSlice(a);
}

/// A terminal shows no option picture or preview: one note for the whole card, after core's own.
pub const previews_note = "the console can't show option previews — the choices are listed by name";

/// Core's warnings in its own words and order, then the console's previews note when it applies.
fn warnings(a: std.mem.Allocator, doc: ObjectMap) error{OutOfMemory}![][]const u8 {
    var out: std.ArrayList([]const u8) = .empty;
    for (doc.get("warnings").?.array.items) |w| try out.append(a, w.object.get("message").?.string);
    if (hasPreviews(doc)) try out.append(a, previews_note);
    return out.toOwnedSlice(a);
}

/// Whether the card carried any picture or preview — which a terminal drops with one note.
fn hasPreviews(doc: ObjectMap) bool {
    const ask = doc.get("ask").?;
    if (ask == .object) for (ask.object.get("options").?.array.items) |o| {
        if (o.object.get("actions") != null or o.object.get("image") != null) return true;
    };
    for (doc.get("warnings").?.array.items) |w| {
        if (std.mem.eql(u8, w.object.get("code").?.string, "W_PREVIEW_DROPPED")) return true;
    }
    return false;
}

test {
    _ = @import("validate_test.zig");
}

// Test helpers (after the first test, so the layering lint reads them as test code).
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

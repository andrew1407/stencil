//! The `actions` array of an op-plan: every entry checked against opRegistry.json's key
//! schema (opSchema.zig), then normalized into a typed `Action`. An unknown op is a warning
//! and a skipped action; a known op with bad params rejects the whole plan.
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
const Action = model.Action;
const Variant = model.Variant;
const findOp = model.findOp;
const isMisplacedInVariant = model.isMisplacedInVariant;
const fill = normalize.fill;
const ValidateError = validate.ValidateError;
const NestedError = validate.NestedError;
const droppedVariantWarning = validate.droppedVariantWarning;


/// Validate one actions list: unknown ops drop with a warning (forward compatibility);
/// a known op with invalid params fails the whole plan. With `misplaced` set (inside a
/// variant or preview) a top-level-only/settings op names itself there and returns
/// `error.Misplaced` — the caller drops that holder, never the plan (§1).
pub fn validateActions(
    a: std.mem.Allocator,
    value: ?std.json.Value,
    warnings: *std.ArrayList([]const u8),
    where: []const u8,
    misplaced: ?*?[]const u8,
    diag: *Diag,
) NestedError![]Action {
    const v = value orelse return &.{};
    if (v == .null) return &.{};
    const schema = opSchema.get();
    try schema.checkEnvelope(a, diag, v, "actions", where); // an array ≤ MAX_ACTIONS of objects

    var out: std.ArrayList(Action) = .empty;
    for (v.array.items) |raw| {
        const action = raw.object;
        const op_v = action.get("op");
        if (op_v == null or op_v.? != .string) {
            diag.prefix = "invalid plan: ";
            return diag.fail("every action in \"{s}\" must be an object with an \"op\"", .{where});
        }
        const op = op_v.?.string;
        // §13: a forbidden ("never model-drivable") name is rejected outright — a hard
        // tooth distinct from the unknown-op skip, even though no such op is registered.
        if (schema.isForbidden(op)) {
            diag.prefix = "invalid plan: ";
            return diag.fail("the \"{s}\" op is never model-drivable", .{op});
        }
        const entry = schema.find(op) orelse {
            try warnings.append(a, try std.fmt.allocPrint(a, "Skipped unknown operation \"{s}\"", .{op}));
            continue;
        };
        if (misplaced) |m| {
            if (isMisplacedInVariant(entry)) {
                m.* = op;
                return error.Misplaced;
            }
        }
        const folded = try schema.validateAction(a, diag, action, entry);
        const n = try schema.normalize(a, folded, entry);
        try out.append(a, try fill(a, entry, n, diag));
    }
    return out.toOwnedSlice(a);
}

/// An action's field value, with an explicit JSON `null` treated as absent.
pub fn nonNullField(action: ObjectMap, key: []const u8) ?std.json.Value {
    const v = action.get(key) orelse return null;
    return if (v == .null) null else v;
}

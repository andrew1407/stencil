//! Registry-driven op-plan schema (contract §1–§2, §11): the cli port of
//! browser/js/llm/plan/opSchema.js over the embedded opRegistry.json. Generic checks only —
//! profile membership, unknown keys, required/types/enums/ranges/caps/grammars and the
//! cross-field rules (forms / together / exclusive / minFields / onlyWith /
//! requiredWith). opplan.zig keeps the typed normalizers, executors and cli extras.
const std = @import("std");

/// The canonical registry (browser/js/config/llm/opRegistry.json), embedded whole.
pub const registry_json = @embedFile("opRegistry.json");
pub const surface = "cli";
pub const profile = "console";

pub const Value = json.Value;
pub const ObjectMap = json.ObjectMap;
pub const Error = json.Error;

/// One op this surface registers, resolved for it (surfaceKeys / bulletVariants /
/// surfaceFlags applied).
pub const Entry = struct {
    name: []const u8,
    raw: ObjectMap, // the registry entry: forms / together / exclusive / minFields / rules
    keys: ObjectMap,
    rules: []const []const u8,
    bullet: ?[]const u8,
    addendum: ?[]const u8, // the console `{addendum}` line riding after the bullets
    top_level_only: bool,
    settings: bool, // editorSetting | consoleSetting — never inside variants (§10)
    deferred: bool,
};

pub const OpsetHit = union(enum) { entry: Entry, fail, none };

pub const grammars = @import("opSchema/grammars.zig");
pub const json = @import("opSchema/json.zig");
const ctxm = @import("opSchema/ctx.zig");
const checks = @import("opSchema/checks.zig");
const fields = @import("opSchema/fields.zig");
const load = @import("opSchema/load.zig");

pub const Grammar = grammars.Grammar;
pub const matches = grammars.matches;
pub const Diag = ctxm.Diag;
const Ctx = ctxm.Ctx;
const getObj = json.getObj;
const getArr = json.getArr;
const getStr = json.getStr;
const strEq = json.strEq;
const numOf = json.numOf;
const cropAspectFold = fields.cropAspectFold;
const checkFields = fields.checkFields;
const pickFields = fields.pickFields;
const checkValue = checks.checkValue;
const resolveEntry = load.resolveEntry;

// the schema: the registry resolved for this surface

pub const Schema = struct {
    root: ObjectMap,
    limits: ObjectMap,
    regexes: ObjectMap,
    entries: []const Entry,
    forbidden: []const []const u8,
    ask: ObjectMap, // the §11 card's key schema (ask.schema)
    default_custom_label: []const u8,
    envelope: ObjectMap,

    pub fn find(self: *const Schema, op: []const u8) ?*const Entry {
        for (self.entries) |*e| {
            if (std.mem.eql(u8, e.name, op)) return e;
        }
        return null;
    }

    pub fn isForbidden(self: *const Schema, op: []const u8) bool {
        for (self.forbidden) |name| {
            if (std.mem.eql(u8, name, op)) return true;
        }
        return false;
    }

    /// A cap is a number or a dotted name into `limits` ("MAX_ACTIONS", "ask.label").
    pub fn limit(self: *const Schema, v: Value) f64 {
        return switch (v) {
            .string => |name| self.limitNamed(name),
            else => numOf(v) orelse std.debug.panic("opRegistry: bad limit", .{}),
        };
    }

    pub fn limitNamed(self: *const Schema, name: []const u8) f64 {
        var cur: Value = .{ .object = self.limits };
        var it = std.mem.splitScalar(u8, name, '.');
        while (it.next()) |part| {
            if (cur != .object) break;
            cur = cur.object.get(part) orelse .null;
        }
        return numOf(cur) orelse std.debug.panic("opRegistry: unknown limit \"{s}\"", .{name});
    }

    pub fn describe(self: *const Schema, g: []const u8) []const u8 {
        const d = getObj(self.regexes, "describe") orelse return g;
        return getStr(d, g) orelse g;
    }

    /// Validate one action against its entry (native rules first). Returns the action
    /// as validated (post-fold) — feed it to `normalize`. Fails with "invalid <op> action: …".
    pub fn validateAction(self: *const Schema, a: std.mem.Allocator, diag: *Diag, action: ObjectMap, entry: *const Entry) Error!ObjectMap {
        diag.prefix = try std.fmt.allocPrint(a, "invalid {s} action: ", .{entry.name});
        const ctx = Ctx{ .a = a, .diag = diag, .schema = self };
        var v = action;
        for (entry.rules) |rule| {
            if (std.mem.eql(u8, rule, "cropAspectFold")) {
                v = try cropAspectFold(ctx, v);
            } else std.debug.panic("opRegistry: unknown native rule \"{s}\"", .{rule});
        }
        try checkFields(ctx, v, entry.keys, entry.raw, null, &.{"op"});
        return v;
    }

    /// The declared keys present (deep-picked, trims applied) plus defaults.
    pub fn normalize(_: *const Schema, a: std.mem.Allocator, v: ObjectMap, entry: *const Entry) Error!ObjectMap {
        return pickFields(a, v, entry.keys);
    }

    /// The §11 card's structure (option `actions` only shallowly — the caller validates
    /// them as nested actions). Fails with "invalid plan: …".
    pub fn validateAsk(self: *const Schema, a: std.mem.Allocator, diag: *Diag, ask: Value) Error!void {
        diag.prefix = "invalid plan: ";
        const ctx = Ctx{ .a = a, .diag = diag, .schema = self };
        if (ask != .object) return ctx.fail("\"ask\" must be an object", .{});
        try checkFields(ctx, ask.object, getObj(self.ask, "keys").?, self.ask, .{ .root = "ask." }, &.{});
    }

    pub fn normalizeAsk(self: *const Schema, a: std.mem.Allocator, ask: ObjectMap) Error!ObjectMap {
        return pickFields(a, ask, getObj(self.ask, "keys").?);
    }

    /// Check one envelope slot ("actions" / "variants") shallowly, labelled `as`
    /// ("actions", "variant 2", "ask option 1"). Fails with "invalid plan: …".
    pub fn checkEnvelope(self: *const Schema, a: std.mem.Allocator, diag: *Diag, v: Value, key: []const u8, as: []const u8) Error!void {
        diag.prefix = "invalid plan: ";
        const ctx = Ctx{ .a = a, .diag = diag, .schema = self };
        try checkValue(ctx, v, getObj(self.envelope, key).?, .{ .key = as }, null);
    }

    /// Resolve an op inside a nested op set (§8 open.actions): an entry to validate
    /// with, `fail` for a listed-but-disallowed op, or `none` for an unknown one.
    pub fn opsetEntry(self: *const Schema, name: []const u8, op: []const u8) OpsetHit {
        const os = getObj(getObj(self.root, "opsets").?, name) orelse std.debug.panic("opRegistry: unknown opset \"{s}\"", .{name});
        if (getObj(os, "overrides")) |ov| {
            if (getObj(ov, op)) |o| return .{ .entry = .{
                .name = op,
                .raw = o,
                .keys = getObj(o, "keys").?,
                .rules = &.{},
                .bullet = null,
                .addendum = null,
                .top_level_only = false,
                .settings = false,
                .deferred = false,
            } };
        }
        for (getArr(os, "ops").?) |x| {
            if (strEq(x, op)) {
                for (getArr(self.root, "ops").?) |e| {
                    if (strEq(e.object.get("id").?, op)) return .{ .entry = resolveEntry(e.object) catch return .none };
                }
                return .none;
            }
        }
        if (getArr(os, "failOps")) |fo| for (fo) |x| {
            if (strEq(x, op)) return .fail;
        };
        return .none;
    }
};

/// The registry resolved for the cli (lazy; the first call parses the embedded JSON).
pub fn get() *const Schema {
    return load.get();
}

const testing = std.testing;

test "schema: the cli's console entries, forbidden set and limits come from the registry" {
    const s = get();
    const expected = [_][]const u8{
        "crop",      "rotate", "filter",   "layout",  "formula", "page",   "blank",     "undo",
        "redo",      "reset",  "frame",    "image",   "save",    "accent", "connect",   "disconnect",
        "reconnect", "delete", "openFile", "openUrl", "copy",    "clear",  "clearChat",
    };
    try testing.expectEqual(expected.len, s.entries.len);
    for (expected, s.entries) |name, e| try testing.expectEqualStrings(name, e.name);
    // surfaceKeys.cli: crop's spec takes `album`; copy is field-less.
    try testing.expect(getObj(s.find("crop").?.keys, "spec").?.get("fields").?.object.get("album") != null);
    try testing.expectEqual(@as(usize, 0), s.find("copy").?.keys.count());
    // Flags merge, bullets resolve per surface/profile, the console addendum rides crop.
    try testing.expect(s.find("openFile").?.top_level_only and s.find("openFile").?.settings);
    try testing.expect(s.find("reconnect").?.settings and !s.find("reconnect").?.top_level_only);
    try testing.expect(s.find("clearChat").?.deferred);
    try testing.expect(s.find("redo").?.bullet == null);
    try testing.expect(std.mem.indexOf(u8, s.find("save").?.bullet.?, "~/Downloads") != null);
    try testing.expect(std.mem.indexOf(u8, s.find("crop").?.addendum.?, "\"album\": true") != null);
    try testing.expect(s.isForbidden("endpoint") and s.isForbidden("unshare") and !s.isForbidden("chat"));
    try testing.expectEqual(@as(f64, 16), s.limitNamed("MAX_ACTIONS"));
    try testing.expectEqual(@as(f64, 80), s.limitNamed("ask.label"));
    try testing.expectEqualStrings("Something else…", s.default_custom_label);
    try testing.expect(s.opsetEntry("extensionOpen", "rotate") == .entry);
    try testing.expect(s.opsetEntry("extensionOpen", "frame") == .fail);
    try testing.expect(s.opsetEntry("extensionOpen", "zoom") == .none);
}

test {
    _ = grammars;
    _ = json;
    _ = ctxm;
    _ = checks;
    _ = fields;
    _ = load;
}

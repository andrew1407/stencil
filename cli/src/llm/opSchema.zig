//! The op-plan schema is core's (core/opplan, reached through ../core/opplan.zig): the embedded
//! opRegistry.json resolved for the cli once, the surface it describes (entries, forbidden names,
//! limits) read back as JSON, and each model reply walked into core's one result document.
const std = @import("std");
const opplan = @import("../core/opplan.zig");

pub const registry_json = opplan.registry_json;
pub const surface = "cli";
pub const profile = "console";

pub const Value = std.json.Value;
pub const ObjectMap = std.json.ObjectMap;

/// One op this surface registers, as core resolved it (surfaceKeys / bulletVariants / surfaceFlags).
pub const Entry = struct {
    name: []const u8,
    keys: ObjectMap,
    bullet: ?[]const u8,
    addendum: ?[]const u8, // the console `{addendum}` line riding after the bullets
    top_level_only: bool,
    settings: bool, // editorSetting | consoleSetting — never inside variants (§10)
    deferred: bool,
};

pub const Schema = struct {
    handle: opplan.Schema,
    entries: []const Entry,
    forbidden: []const []const u8,
    limits: ObjectMap,
    default_custom_label: []const u8,

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

    /// A cap by its dotted name into `limits` ("MAX_ACTIONS", "ask.label").
    pub fn limitNamed(self: *const Schema, name: []const u8) f64 {
        var cur: Value = .{ .object = self.limits };
        var it = std.mem.splitScalar(u8, name, '.');
        while (it.next()) |part| cur = if (cur == .object) cur.object.get(part) orelse .null else .null;
        return switch (cur) {
            .integer => |i| @floatFromInt(i),
            .float => |f| f,
            else => std.debug.panic("opRegistry: unknown limit \"{s}\"", .{name}),
        };
    }
};

pub const Status = opplan.Status;

/// Core's result for one reply: `doc` is {status, reply, actions, variants, ask, warnings, error}.
pub const Parsed = struct { status: Status, doc: ObjectMap };

// Created once, from the main thread only (scrape's fetch pool never reaches here); lives for the process.
var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
var instance: ?Schema = null;

fn flag(flags: ObjectMap, name: []const u8) bool {
    const v = flags.get(name) orelse return false;
    return v == .bool and v.bool;
}

fn optString(o: ObjectMap, key: []const u8) ?[]const u8 {
    const v = o.get(key) orelse return null;
    return if (v == .string) v.string else null;
}

fn build() error{OutOfMemory}!Schema {
    const a = arena.allocator();
    const h = opplan.Schema.open(surface, null);
    const why = h.failure();
    if (why.len != 0) std.debug.panic("embedded opRegistry.json refused by core: {s}", .{why});
    const doc = (std.json.parseFromSliceLeaky(Value, a, h.entries(), .{ .allocate = .alloc_always }) catch @panic("core wrote malformed schema JSON")).object;
    if (!std.mem.eql(u8, doc.get("profile").?.string, profile)) @panic("opRegistry.json: the cli is no longer a console-profile surface");
    var entries: std.ArrayList(Entry) = .empty;
    for (doc.get("entries").?.array.items) |ev| {
        const e = ev.object;
        const flags = e.get("flags").?.object;
        try entries.append(a, .{
            .name = e.get("name").?.string,
            .keys = e.get("keys").?.object,
            .bullet = optString(e, "bullet"),
            .addendum = optString(e, "addendum"),
            .top_level_only = flag(flags, "topLevelOnly"),
            .settings = flag(flags, "editorSetting") or flag(flags, "consoleSetting"),
            .deferred = flag(flags, "deferred"),
        });
    }
    var forbidden: std.ArrayList([]const u8) = .empty;
    for (doc.get("forbidden").?.array.items) |x| try forbidden.append(a, x.string);
    return .{
        .handle = h,
        .entries = try entries.toOwnedSlice(a),
        .forbidden = try forbidden.toOwnedSlice(a),
        .limits = doc.get("limits").?.object,
        .default_custom_label = doc.get("defaultCustomLabel").?.string,
    };
}

/// The registry resolved for the cli (lazy; the first call hands the embedded JSON to core).
pub fn get() *const Schema {
    if (instance == null) instance = build() catch @panic("out of memory resolving opRegistry.json");
    return &instance.?;
}

/// Walk one model reply through core; the document lives in `a`.
pub fn parse(a: std.mem.Allocator, raw: []const u8) error{OutOfMemory}!Parsed {
    const walked = get().handle.walk(a, raw) catch |e| switch (e) {
        error.OutOfMemory => return error.OutOfMemory,
        error.SchemaRefused => @panic("core refused a reply for a schema it loaded"),
    };
    const doc = std.json.parseFromSliceLeaky(Value, a, walked.json, .{}) catch |e| switch (e) {
        error.OutOfMemory => return error.OutOfMemory,
        else => std.debug.panic("core wrote a malformed plan result", .{}),
    };
    return .{ .status = walked.status, .doc = doc.object };
}

const testing = std.testing;

test "schema: the cli's console entries, forbidden set and limits come from core's resolution" {
    const s = get();
    const expected = [_][]const u8{
        "crop",      "rotate", "filter",   "layout",  "formula", "page",   "blank",     "undo",
        "redo",      "reset",  "frame",    "image",   "save",    "accent", "connect",   "disconnect",
        "reconnect", "delete", "openFile", "openUrl", "copy",    "clear",  "clearChat",
    };
    try testing.expectEqual(expected.len, s.entries.len);
    for (expected, s.entries) |name, e| try testing.expectEqualStrings(name, e.name);
    // surfaceKeys.cli: crop's spec takes `album`; copy is field-less.
    try testing.expect(s.find("crop").?.keys.get("spec").?.object.get("fields").?.object.get("album") != null);
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
}

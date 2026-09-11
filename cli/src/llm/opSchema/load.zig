//! Resolving the embedded opRegistry.json for this surface: per-surface flag/bullet/key
//! overlays, the profile's op order, and the process-lifetime arena the tree lives in.
const std = @import("std");
const json = @import("json.zig");
const root_mod = @import("../opSchema.zig");

const Value = json.Value;
const ObjectMap = json.ObjectMap;
const Entry = root_mod.Entry;
const Schema = root_mod.Schema;
const surface = root_mod.surface;
const profile = root_mod.profile;
const registry_json = root_mod.registry_json;
const getObj = json.getObj;
const getArr = json.getArr;
const getStr = json.getStr;
const getBool = json.getBool;
const strEq = json.strEq;
const inList = json.inList;

/// `map[surface] ?? map[profile]`.
pub fn forSurface(map: ?ObjectMap) ?Value {
    const m = map orelse return null;
    return m.get(surface) orelse m.get(profile);
}

pub fn resolveEntry(e: ObjectMap) error{OutOfMemory}!Entry {
    var flags: ObjectMap = .empty;
    if (getObj(e, "flags")) |f| {
        var it = f.iterator();
        while (it.next()) |kv| try flags.put(arena.allocator(), kv.key_ptr.*, kv.value_ptr.*);
    }
    if (forSurface(getObj(e, "surfaceFlags"))) |sf| {
        var it = sf.object.iterator();
        while (it.next()) |kv| try flags.put(arena.allocator(), kv.key_ptr.*, kv.value_ptr.*);
    }
    var rules: std.ArrayList([]const u8) = .empty;
    if (getArr(e, "rules")) |rs| for (rs) |r| try rules.append(arena.allocator(), r.string);
    var bullet = getStr(e, "bullet");
    var addendum: ?[]const u8 = null;
    if (forSurface(getObj(e, "bulletVariants"))) |variant| switch (variant) {
        .string => |s| bullet = s,
        .object => |o| addendum = getStr(o, "addendum"),
        else => {},
    };
    var keys = getObj(e, "keys").?;
    if (getObj(e, "surfaceKeys")) |sk| {
        if (getObj(sk, surface)) |k| keys = k;
    }
    return .{
        .name = getStr(e, "name").?,
        .raw = e,
        .keys = keys,
        .rules = try rules.toOwnedSlice(arena.allocator()),
        .bullet = bullet,
        .addendum = addendum,
        .top_level_only = getBool(flags, "topLevelOnly"),
        .settings = getBool(flags, "editorSetting") or getBool(flags, "consoleSetting"),
        .deferred = getBool(flags, "deferred"),
    };
}

// Parsed once on first use, from the main thread only (scrape's fetch pool never reaches
// here). The tree lives for the process.
var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
var instance: ?Schema = null;

pub fn build() error{OutOfMemory}!Schema {
    const a = arena.allocator();
    const root = std.json.parseFromSliceLeaky(Value, a, registry_json, .{}) catch @panic("embedded opRegistry.json is malformed");
    const r = root.object;
    const profiles = getObj(r, "profiles").?;
    // Fail-fast pin: the $meta map must still route this surface to its profile.
    const prof = getStr(getObj(getObj(r, "$meta").?, "surfaceProfiles").?, surface).?;
    if (!std.mem.eql(u8, prof, profile)) @panic("opRegistry.json: the cli is no longer a console-profile surface");
    const order = getArr(getObj(profiles, profile).?, "ops").?;
    // This surface's entries in the profile's (= prompt) order, minus entries restricted
    // to other surfaces.
    var entries: std.ArrayList(Entry) = .empty;
    for (order) |name| {
        for (getArr(r, "ops").?) |ev| {
            const e = ev.object;
            if (!strEq(e.get("name").?, name.string)) continue;
            if (!inList(.{ .string = profile }, e.get("profiles").?)) continue;
            if (e.get("surfaces")) |s| {
                if (!inList(.{ .string = surface }, s)) continue;
            }
            try entries.append(a, try resolveEntry(e));
        }
    }
    var forbidden: std.ArrayList([]const u8) = .empty;
    for (getArr(getObj(getObj(r, "forbidden").?, "perSurface").?, surface).?) |x| try forbidden.append(a, x.string);
    const ask = getObj(r, "ask").?;
    return .{
        .root = r,
        .limits = getObj(r, "limits").?,
        .regexes = getObj(r, "regexes").?,
        .entries = try entries.toOwnedSlice(a),
        .forbidden = try forbidden.toOwnedSlice(a),
        .ask = getObj(ask, "schema").?,
        .default_custom_label = getStr(ask, "defaultCustomLabel").?,
        .envelope = getObj(r, "envelope").?,
    };
}

/// The registry resolved for the cli (lazy; the first call parses the embedded JSON).
pub fn get() *const Schema {
    if (instance == null) instance = build() catch @panic("out of memory parsing opRegistry.json");
    return &instance.?;
}

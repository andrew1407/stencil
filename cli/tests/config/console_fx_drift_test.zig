//! Drift guard for the console's animation numbers: the browser and desktop CLI modes read
//! common/config/console/fx.json, and the CLI keeps these as constants beside the code that
//! paces on them, so every one is pinned to the canonical table here.
const std = @import("std");
const reveal = @import("../../src/console/render/logoFx/reveal.zig");
const press = @import("../../src/console/render/logoFx/press.zig");
const rain = @import("../../src/console/render/logoFx/rain.zig");
const clock = @import("../../src/console/render/logoFx/clock.zig");
const prefs = @import("../../src/console/screen/prefs.zig");
const testing = std.testing;

const fx_json = @embedFile("fx.json");
const motion_json = @embedFile("motion.json");

fn num(section: std.json.ObjectMap, key: []const u8) !f64 {
    const v = section.get(key) orelse {
        std.debug.print("fx.json has no '{s}'\n", .{key});
        return error.MissingFxNumber;
    };
    return switch (v) {
        .integer => |i| @floatFromInt(i),
        .float => |f| f,
        else => error.MissingFxNumber,
    };
}

test "the console's reveal, wipe, press and rain numbers are fx.json's" {
    const parsed = try std.json.parseFromSlice(std.json.Value, testing.allocator, fx_json, .{});
    defer parsed.deinit();
    const root = parsed.value.object;
    const r = root.get("reveal").?.object;
    try testing.expectEqual(@as(f64, reveal.reveal_step_ms), try num(r, "stepMs"));
    try testing.expectEqual(@as(f64, reveal.reveal_jumps), try num(r, "jumps"));
    try testing.expectEqual(@as(f64, reveal.reveal_burst_jumps), try num(r, "burstJumps"));
    try testing.expectEqual(@as(f64, reveal.reveal_quiet_ms), try num(r, "quietMs"));
    try testing.expectEqual(@as(f64, reveal.reveal_burst_max_ms), try num(r, "burstMaxMs"));
    try testing.expectEqual(@as(f64, reveal.reveal_burst_ceiling_ms), try num(r, "ceilingMs"));
    const w = root.get("wipe").?.object;
    try testing.expectEqual(@as(f64, press.wipe_jumps), try num(w, "jumps"));
    try testing.expectEqual(@as(f64, press.wipe_step_ms), try num(w, "stepMs"));
    try testing.expectEqual(@as(f64, press.flourish_step_ms), try num(w, "flourishStepMs"));
    try testing.expectEqual(@as(f64, press.rule_wipe_pct), try num(w, "ruleWipePct"));
    try testing.expectEqual(@as(f64, clock.icon_cols), try num(w, "iconCols"));
    try testing.expectEqual(clock.cell_aspect, try num(w, "cellAspect"));
    try testing.expectEqual(clock.icon_speed, try num(w, "iconSpeed"));
    try testing.expectEqual(@as(f64, press.press_ms), try num(root.get("press").?.object, "ms"));
    const d = root.get("rain").?.object;
    try testing.expectEqual(@as(f64, rain.frames), try num(d, "frames"));
    try testing.expectEqual(@as(f64, rain.frame_ms), try num(d, "frameMs"));
    try testing.expectEqual(@as(f64, rain.trail), try num(d, "trail"));
}

test "the /reveal-speed range is motion.json's consoleSpeed" {
    const parsed = try std.json.parseFromSlice(std.json.Value, testing.allocator, motion_json, .{});
    defer parsed.deinit();
    const s = parsed.value.object.get("consoleSpeed").?.object;
    try testing.expectEqual(prefs.speed_min, try num(s, "min"));
    try testing.expectEqual(prefs.speed_max, try num(s, "max"));
    try testing.expectEqual(prefs.speed_default, try num(s, "default"));
}

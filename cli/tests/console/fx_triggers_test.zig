//! The secret words' triggers: each dispatches (in any case, only with its slash) and toggles
//! its skin, none is reachable from `help` or Tab, the full-screen refusal and the gold list read
//! exactly as pinned, and taking a skin off gives back the console's own look byte for byte.
const std = @import("std");
const console = @import("../../src/console.zig");
const skin = @import("../../src/app/skin.zig");
const logo = @import("../../src/app/logo.zig");
const msg = @import("../../src/app/messages.zig");
const ui = @import("../../src/console/ui.zig");
const commands = @import("../../src/console/commands.zig");
const screen = @import("../../src/console/screen.zig");
const ansi = @import("../../src/console/render/ansi.zig");
const fx = @import("fx_harness.zig");
const loop = @import("../../src/console/loop.zig");
const testing = std.testing;

const Cap = struct {
    buf: std.ArrayList(u8) = .empty,
    fn sink(ctx: *anyopaque, bytes: []const u8) void {
        const self: *Cap = @ptrCast(@alignCast(ctx));
        self.buf.appendSlice(testing.allocator, bytes) catch {};
    }
};

const words = [_]struct { word: []const u8, skin: skin.Skin }{
    .{ .word = "mranderson", .skin = .matrix },
    .{ .word = "theverybluescreen", .skin = .bluescreen },
    .{ .word = "sunafterrain", .skin = .rainbow },
    .{ .word = "theyareinthetrees", .skin = .fruit },
    .{ .word = "meow", .skin = .meow },
    .{ .word = "pieday", .skin = .pie },
    .{ .word = "bifrost", .skin = .bifrost },
    .{ .word = "fairylight", .skin = .fairylight },
};

test "fx triggers: every secret word puts its skin on and takes it off, in any case, slash and all" {
    var rig: fx.Rig = undefined;
    try rig.start(testing.allocator, 24, 80);
    defer rig.deinit();
    rig.scr.setRevealSpeed(1.0);
    for (words) |w| {
        var up: [32]u8 = undefined;
        const loud = std.ascii.upperString(&up, w.word);
        var line: [40]u8 = undefined;
        _ = try console.handle(&rig.session, rig.io, try std.fmt.bufPrint(&line, "{s}", .{w.word}));
        try testing.expectEqual(skin.Skin.none, skin.get()); // bare, it is an unknown command
        _ = try console.handle(&rig.session, rig.io, try std.fmt.bufPrint(&line, "/{s}", .{loud}));
        try testing.expectEqual(w.skin, skin.get());
        _ = try console.handle(&rig.session, rig.io, try std.fmt.bufPrint(&line, "{s}", .{w.word}));
        try testing.expectEqual(w.skin, skin.get()); // …and cannot take it off either
        _ = try console.handle(&rig.session, rig.io, try std.fmt.bufPrint(&line, "  /{s}", .{w.word}));
        try testing.expectEqual(skin.Skin.none, skin.get());
    }
    try testing.expectEqual(words.len, std.enums.values(skin.Skin).len - 1); // every skin is listed here
}

test "fx triggers: no secret word is a command, a help line or a Tab completion" {
    var cap = Cap{};
    defer cap.buf.deinit(testing.allocator);
    logo.setSink(Cap.sink, &cap);
    defer logo.clearSink();
    ui.help();
    for (words) |w| {
        try testing.expect(commands.verbOf(w.word) == null);
        try testing.expect(commands.actionOf(w.word, "") == null);
        try testing.expect(std.mem.indexOf(u8, cap.buf.items, w.word) == null);
        for (ui.completions) |c| try testing.expect(!std.ascii.eqlIgnoreCase(c, w.word));
    }
    try testing.expect(std.mem.indexOf(u8, cap.buf.items, skin.list_word) == null);
    for (ui.completions) |c| try testing.expect(!std.ascii.eqlIgnoreCase(c, skin.list_word));
}

test "fx triggers: outside the full screen a secret only says where it works" {
    var cap = Cap{};
    defer cap.buf.deinit(testing.allocator);
    logo.setSink(Cap.sink, &cap);
    defer logo.clearSink();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    var session = console.Session{ .gpa = testing.allocator };
    defer session.deinit();
    for (words) |w| {
        cap.buf.clearRetainingCapacity();
        var line: [40]u8 = undefined;
        _ = try console.handle(&session, threaded.io(), try std.fmt.bufPrint(&line, "/{s}", .{w.word}));
        try testing.expectEqualStrings("that one only works in --console-full-screen\n", cap.buf.items);
        try testing.expectEqualStrings(msg.eggs_full_screen_only, cap.buf.items);
        try testing.expectEqual(skin.Skin.none, skin.get());
    }
}

test "fx triggers: /eastereggs is the words in gold, and each phrase is gold too" {
    var rig: fx.Rig = undefined;
    try rig.start(testing.allocator, 24, 200); // wide enough that no phrase wraps
    defer rig.deinit();
    rig.scr.setRevealSpeed(1.0);
    _ = try console.handle(&rig.session, rig.io, "/eastereggs");
    const lines = &rig.scr.lines;
    for (words, lines.len - words.len..) |w, at| {
        const got = lines.at(at);
        var want: [64]u8 = undefined;
        try testing.expectEqualStrings(try std.fmt.bufPrint(&want, "{s}/{s}\x1b[0m", .{ skin.gold, w.word }), got);
    }
    _ = try console.handle(&rig.session, rig.io, "/bifrost");
    const last = rig.scr.lines.back().?;
    try testing.expectEqualStrings(skin.gold ++ "Whosoever activates this secret, if they be worthy, shall possess the power of the Stencil.\x1b[0m", last);
    try testing.expectEqualStrings(skin.gold ++ msg.bifrost_on[0 .. msg.bifrost_on.len - 1] ++ "\x1b[0m", last);
}

test "fx triggers: a bare secret word is no secret — no list, no skin, no gold on its echo" {
    var rig: fx.Rig = undefined;
    try rig.start(testing.allocator, 24, 200);
    defer rig.deinit();
    rig.scr.setRevealSpeed(1.0);
    const before = rig.scr.lines.len;
    _ = try console.handle(&rig.session, rig.io, "eastereggs");
    for (before..rig.scr.lines.len) |i| try testing.expect(std.mem.indexOf(u8, rig.scr.lines.at(i), "mranderson") == null);
    loop.echoCommand(&rig.session, &rig.scr, "meow");
    try testing.expect(std.mem.indexOf(u8, rig.scr.lines.back().?, skin.gold) == null);
    loop.echoCommand(&rig.session, &rig.scr, "/meow");
    try testing.expect(std.mem.indexOf(u8, rig.scr.lines.back().?, skin.gold) != null);
}

/// The header and the two rules as a plain paint draws them, captured off the fake terminal.
fn chrome(rig: *fx.Rig) ![]u8 {
    rig.frame.clearRetainingCapacity();
    rig.scr.paintHeader();
    rig.scr.drawStatusBar();
    return testing.allocator.dupe(u8, fx.stripWrappers(rig.frame.items));
}

test "fx triggers: taking a skin off paints the console's own look again, byte for byte" {
    var rig: fx.Rig = undefined;
    try rig.start(testing.allocator, 24, 80);
    defer rig.deinit();
    rig.scr.setRevealSpeed(1.0);
    const accent = logo.accentRgb();
    const before = try chrome(&rig);
    defer testing.allocator.free(before);
    const seeded = rig.scr.lines.at(1);
    var rb: [512]u8 = undefined;
    const row_before = try testing.allocator.dupe(u8, ansi.clip(seeded, rig.scr.cols, &rb));
    defer testing.allocator.free(row_before);
    for (words) |w| {
        var line: [40]u8 = undefined;
        _ = try console.handle(&rig.session, rig.io, try std.fmt.bufPrint(&line, "/{s}", .{w.word}));
        try testing.expectEqual(w.skin, skin.get());
        _ = try console.handle(&rig.session, rig.io, try std.fmt.bufPrint(&line, "/{s}", .{w.word}));
        const after = try chrome(&rig);
        defer testing.allocator.free(after);
        try testing.expectEqualStrings(before, after);
        try testing.expectEqualStrings(row_before, ansi.clip(seeded, rig.scr.cols, &rb));
        try testing.expectEqual(accent, logo.accentRgb());
        try testing.expectEqual(accent, rig.scr.painted_accent);
    }
}

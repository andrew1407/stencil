//! Drift guard between the console's prose asset (src/console/uiText.txt) and the command
//! grammar in src/console/commands.zig. The `/help` block and `verbOf`/`actionOf` are two
//! hand-kept lists of the same commands: a documented command the parser never accepts, or
//! a verb nobody documented, fails here. The wording itself is pinned byte-for-byte by
//! tests/pins/{intro,help,filters}.*.txt — this test is about the list, not the layout.
const std = @import("std");
const commands = @import("../../src/console/commands.zig");
const testing = std.testing;

const ui_text = @embedFile("../../src/console/uiText.txt");

// In `/help` but not a command word: the Shortcuts footer's key names and the argument
// placeholders that happen to start with '/'.
const not_commands = [_][]const u8{ "/prompt,", "/save;" };

// A command with no `/help` row, left as found because tests/pins/help.*.txt are byte goldens and
// rewording the prose is a separate change.
const undocumented = [_][]const u8{"project_description"};

/// The lines of the '@name' block, without its trailing newline (ui.zig's own reader).
fn block(comptime name: []const u8) []const u8 {
    const at = std.mem.indexOf(u8, ui_text, "\n@" ++ name ++ "\n").? + name.len + 3;
    const rest = ui_text[at..];
    const end = std.mem.indexOf(u8, rest, "\n@") orelse return rest[0 .. rest.len - 1];
    return rest[0..end];
}

/// Every '/word' the help block mentions, deduped.
fn documented(a: std.mem.Allocator) !std.StringHashMap(void) {
    var out = std.StringHashMap(void).init(a);
    const text = block("help");
    var i: usize = 0;
    while (i < text.len) : (i += 1) {
        if (text[i] != '/') continue;
        if (i != 0 and text[i - 1] != ' ' and text[i - 1] != '\n' and text[i - 1] != '\t') continue;
        var n = i + 1;
        while (n < text.len and (std.ascii.isLower(text[n]) or text[n] == '-')) n += 1;
        if (n == i + 1) continue;
        try out.put(text[i..n], {});
        i = n;
    }
    return out;
}

test "every block the console prints exists in uiText.txt" {
    inline for (.{ "intro", "intro-fullscreen", "mouse-app", "mouse-terminal", "filters", "help" }) |name| {
        try testing.expect(block(name).len > 0);
    }
}

test "help: every documented command is one the console dispatches" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    var docs = try documented(arena.allocator());
    try testing.expect(docs.count() >= 40); // the block really was scanned

    var unknown: usize = 0;
    var it = docs.keyIterator();
    while (it.next()) |slash| {
        const word = slash.*[1..];
        if (commands.verbOf(word) != null or commands.actionOf(word, "") != null) continue;
        var excused = false;
        for (not_commands) |e| excused = excused or std.mem.eql(u8, slash.*, e);
        if (excused) continue;
        std.debug.print("/help documents '{s}', which is not a command\n", .{slash.*});
        unknown += 1;
    }
    try testing.expectEqual(@as(usize, 0), unknown);
}

test "help: every console verb is documented" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    var docs = try documented(arena.allocator());

    var missing: usize = 0;
    inline for (@typeInfo(commands.Verb).@"enum".fields) |f| {
        const want: commands.Verb = @enumFromInt(f.value);
        var found = false;
        var it = docs.keyIterator();
        while (it.next()) |slash| {
            if (commands.verbOf(slash.*[1..])) |v| found = found or v == want;
        }
        var excused = false;
        for (undocumented) |e| excused = excused or std.mem.eql(u8, e, f.name);
        if (!found and !excused) {
            std.debug.print("no /help row documents the '{s}' command\n", .{f.name});
            missing += 1;
        }
        if (found and excused) {
            std.debug.print("'{s}' is now documented — drop it from `undocumented`\n", .{f.name});
            missing += 1;
        }
    }
    try testing.expectEqual(@as(usize, 0), missing);
}

test "filters: the list names every mode /filter accepts" {
    const text = block("filters");
    inline for (.{ "bw", "sepia", "invert", "contour", "none" }) |mode| {
        if (std.mem.indexOf(u8, text, "   " ++ mode ++ " ") == null) {
            std.debug.print("the /filter listing never names '{s}'\n", .{mode});
            return error.FilterModeUndocumented;
        }
        try testing.expect(commands.actionOf(mode, "") != null);
    }
}

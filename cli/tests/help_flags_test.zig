//! Drift guard between the CLI's two hand-kept flag lists: the `eq(arg, "--x")` arms of
//! src/args.zig's parser and the `--help` prose in src/help.txt. Neither generates the
//! other (the parser's arms carry per-flag rules, the prose carries wording), so this test
//! is what makes them agree: a documented flag the parser does not accept, or a newly
//! parsed flag nobody documented, fails here. `undocumented` is the explicit exception
//! list — flags that exist but are deliberately absent from --help.
const std = @import("std");
const testing = std.testing;

const args_src = @embedFile("../src/args.zig");
const help_text = @embedFile("../src/help.txt");

// Accepted by the parser, absent from --help on purpose: two spelling aliases, and the
// collaboration-server flags, which the README documents instead.
const undocumented = [_][]const u8{
    "--console-fullscreen", "--remote", "--remote-name", "--remote-update",
    "--repl",               "--server", "--token",
};

const Set = std.StringHashMap(void);

/// Every flag spelling the parser matches on: the string in each `eq(arg, "…")` arm.
fn parsedFlags(a: std.mem.Allocator) !Set {
    var out = Set.init(a);
    const needle = "eq(arg, \"";
    var i: usize = 0;
    while (std.mem.indexOfPos(u8, args_src, i, needle)) |at| {
        const start = at + needle.len;
        const end = std.mem.indexOfScalarPos(u8, args_src, start, '"') orelse break;
        const word = args_src[start..end];
        if (word.len > 1 and word[0] == '-') try out.put(word, {});
        i = end;
    }
    return out;
}

/// Every flag spelling the help prose shows: a `-x` / `--word` token at a word boundary.
/// Numbers ("-1 = -90") and mid-word hyphens ("'|'-joined") are not flags.
fn documentedFlags(a: std.mem.Allocator) !Set {
    var out = Set.init(a);
    var i: usize = 0;
    while (i < help_text.len) : (i += 1) {
        if (help_text[i] != '-') continue;
        if (i != 0 and help_text[i - 1] != ' ' and help_text[i - 1] != '\n') continue;
        var n = i + 1;
        if (n < help_text.len and help_text[n] == '-') n += 1;
        if (n >= help_text.len or !std.ascii.isLower(help_text[n])) continue;
        while (n < help_text.len and (std.ascii.isLower(help_text[n]) or help_text[n] == '-')) n += 1;
        try out.put(help_text[i..n], {});
        i = n;
    }
    return out;
}

test "help: every documented flag is one the parser accepts" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();

    var parsed = try parsedFlags(a);
    var documented = try documentedFlags(a);
    try testing.expect(parsed.count() >= 30 and documented.count() >= 25); // both really scanned

    var missing: usize = 0;
    var it = documented.keyIterator();
    while (it.next()) |flag| {
        if (parsed.contains(flag.*)) continue;
        std.debug.print("--help documents '{s}', which args.zig does not parse\n", .{flag.*});
        missing += 1;
    }
    try testing.expectEqual(@as(usize, 0), missing);
}

test "help: every parsed flag is documented, or listed as deliberately not" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();

    var parsed = try parsedFlags(a);
    var documented = try documentedFlags(a);
    var exempt = Set.init(a);
    for (undocumented) |f| try exempt.put(f, {});

    var undoc: usize = 0;
    var it = parsed.keyIterator();
    while (it.next()) |flag| {
        if (documented.contains(flag.*) or exempt.contains(flag.*)) continue;
        std.debug.print("args.zig parses '{s}' but --help never mentions it\n", .{flag.*});
        undoc += 1;
    }
    try testing.expectEqual(@as(usize, 0), undoc);

    // The exception list must not outlive the flags it excuses.
    for (undocumented) |f| {
        if (parsed.contains(f)) continue;
        std.debug.print("'{s}' is listed as undocumented but no longer parsed\n", .{f});
        undoc += 1;
    }
    try testing.expectEqual(@as(usize, 0), undoc);
}

test "help: the parser really accepts every flag the prose shows" {
    const a = testing.allocator;
    var arena = std.heap.ArenaAllocator.init(a);
    defer arena.deinit();
    var documented = try documentedFlags(arena.allocator());

    const parse = @import("../src/args.zig").parse;
    const Error = @import("../src/args.zig").Error;
    var it = documented.keyIterator();
    while (it.next()) |flag| {
        var buf: [64]u8 = undefined;
        @memcpy(buf[0..flag.len], flag.*);
        buf[flag.len] = 0;
        const argv = [_][:0]const u8{buf[0..flag.len :0]};
        // A value-taking flag with nothing after it is MissingValue; only UnknownFlag means
        // the prose names something the parser never sees.
        _ = parse(a, &argv) catch |e| {
            if (e == Error.UnknownFlag) {
                std.debug.print("parse() rejects documented flag '{s}'\n", .{flag.*});
                return error.DocumentedFlagRejected;
            }
        };
    }
}

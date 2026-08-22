// Walks the shared op-plan conformance corpus (browser/js/config/llm/fixtures/opPlan/)
// against the REAL cli validator (llm.parsePlan — the entry handlers.zig doPrompt uses).
// Port of the reference walker browser/tests/opPlanFixtures.test.js, with the cli's
// profile ("console"). Verdict = local override ?? knownDivergence.cli ?? expect;
// "valid" = parsePlan returns a plan (chat-only counts), "invalid" = it rejects.
const std = @import("std");
const llm = @import("../src/llm.zig");
const fx = @import("fixture_corpus.zig");
const testing = std.testing;

const opplan_dir = "llm/fixtures/opPlan/";

const known_profiles = [_][]const u8{ "editor", "console", "bot", "mcp", "extension", "all" };
const known_surfaces = [_][]const u8{ "browser", "desktop", "cli", "pystencil", "bot", "mcp", "extension" };
const cli_profiles = [_][]const u8{ "console", "all" };

fn oneOf(s: []const u8, set: []const []const u8) bool {
    for (set) |x| {
        if (std.mem.eql(u8, s, x)) return true;
    }
    return false;
}

test "opPlan corpus: exists and is well-formed (the reference walker's shape check)" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    const files = try fx.listJson(a, io, opplan_dir);
    try testing.expect(files.len >= 80); // a real corpus

    for (files) |file| {
        if (std.mem.eql(u8, file, "_schema.md")) continue;
        var path_buf: [256]u8 = undefined;
        const sub = try std.fmt.bufPrint(&path_buf, "{s}{s}", .{ opplan_dir, file });
        const f = try fx.loadJson(a, io, sub);

        // "name" must match the filename slug (NNN- prefix + .json stripped).
        const slug_start = (std.mem.indexOfScalar(u8, file, '-') orelse 0) + 1;
        const slug = file[slug_start .. file.len - ".json".len];
        try testing.expectEqualStrings(slug, fx.memberStr(f, "name").?);

        const profiles = fx.member(f, "profiles").?;
        try testing.expect(profiles == .array and profiles.array.items.len > 0);
        for (profiles.array.items) |p| try testing.expect(oneOf(p.string, &known_profiles));

        const expect_s = fx.memberStr(f, "expect").?;
        try testing.expect(oneOf(expect_s, &.{ "valid", "invalid" }));
        try testing.expect(fx.member(f, "input") != null);
        if (std.mem.eql(u8, expect_s, "invalid"))
            try testing.expect(fx.memberStr(f, "reason").?.len > 0);

        if (fx.member(f, "knownDivergence")) |kd| {
            var it = kd.object.iterator();
            while (it.next()) |kv| {
                try testing.expect(oneOf(kv.key_ptr.*, &known_surfaces));
                try testing.expect(oneOf(kv.value_ptr.string, &.{ "valid", "invalid" }));
            }
        }
    }
}

test "opPlan corpus: every console fixture parses to its pinned verdict" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    const overrides = try fx.parseOverrides(a);
    const files = try fx.listJson(a, io, opplan_dir);
    var walked: usize = 0;
    var skipped: usize = 0;
    var failures: usize = 0;

    for (files) |file| {
        var path_buf: [256]u8 = undefined;
        const sub = try std.fmt.bufPrint(&path_buf, "{s}{s}", .{ opplan_dir, file });
        const f = try fx.loadJson(a, io, sub);
        const name = fx.memberStr(f, "name").?;

        var applies = false;
        for (fx.member(f, "profiles").?.array.items) |p| {
            if (oneOf(p.string, &cli_profiles)) applies = true;
        }
        if (!applies) {
            skipped += 1;
            continue;
        }
        walked += 1;

        // Verdict precedence: local override ?? knownDivergence.cli ?? expect.
        var want = fx.memberStr(f, "expect").?;
        if (fx.member(f, "knownDivergence")) |kd| {
            if (fx.memberStr(kd, "cli")) |v| want = v;
        }
        if (fx.overrideFor(overrides, "opPlan", name)) |ov| {
            if (fx.memberStr(ov, "verdict")) |v| want = v;
        }

        const input = fx.member(f, "input").?;
        const text = if (input == .string) input.string else try fx.stringify(a, input);

        var got: []const u8 = undefined;
        var detail: []const u8 = "";
        switch (try llm.parsePlan(testing.allocator, text)) {
            .plan => |p| {
                var plan = p;
                defer plan.deinit();
                got = "valid";
            },
            .invalid => |msg| {
                detail = try a.dupe(u8, msg);
                testing.allocator.free(msg);
                got = "invalid";
            },
        }
        if (!std.mem.eql(u8, got, want)) {
            std.debug.print("opPlan {s}: want {s}, cli says {s} {s}\n", .{ file, want, got, detail });
            failures += 1;
        }
    }
    std.debug.print("opPlan corpus: walked {d}, skipped {d} (profiles exclude console/all)\n", .{ walked, skipped });
    try testing.expectEqual(@as(usize, 0), failures);
}

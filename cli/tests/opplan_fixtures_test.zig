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

const Entry = struct { file: []const u8, value: std.json.Value };

// The hand-written files plus the registry-generated bundle (generated/cases.json,
// browser/tools/genOpPlanFixtures.mjs), each generated case walking as "<name>.json".
fn loadCorpus(w: *fx.Walk) ![]Entry {
    const a = w.alloc();
    const io = w.io();
    var out: std.ArrayList(Entry) = .empty;
    for (try fx.listJson(a, io, opplan_dir)) |file| {
        if (std.mem.eql(u8, file, "_schema.md")) continue;
        const sub = try std.fmt.allocPrint(a, "{s}{s}", .{ opplan_dir, file });
        try out.append(a, .{ .file = file, .value = try fx.loadJson(a, io, sub) });
    }
    const bundle = try fx.loadJson(a, io, opplan_dir ++ "generated/cases.json");
    for (fx.member(bundle, "cases").?.array.items) |c| {
        const file = try std.fmt.allocPrint(a, "{s}.json", .{fx.memberStr(c, "name").?});
        try out.append(a, .{ .file = file, .value = c });
    }
    return out.items;
}

fn oneOf(s: []const u8, set: []const []const u8) bool {
    for (set) |x| {
        if (std.mem.eql(u8, s, x)) return true;
    }
    return false;
}

test "opPlan corpus: exists and is well-formed (the reference walker's shape check)" {
    var w = fx.Walk.start();
    defer w.stop();

    const entries = try loadCorpus(&w);
    try testing.expect(entries.len >= 300); // a real corpus

    for (entries) |ent| {
        const file = ent.file;
        const f = ent.value;

        // "name" must match the filename slug (a numeric NNN- prefix + .json stripped;
        // generated cases carry no prefix).
        const dash = std.mem.indexOfScalar(u8, file, '-') orelse 0;
        const numeric = dash > 0 and blk: {
            for (file[0..dash]) |ch| if (!std.ascii.isDigit(ch)) break :blk false;
            break :blk true;
        };
        const slug_start: usize = if (numeric) dash + 1 else 0;
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
    var w = fx.Walk.start();
    defer w.stop();
    const a = w.alloc();

    try w.loadOverrides();
    for (try loadCorpus(&w)) |ent| {
        const file = ent.file;
        const f = ent.value;
        const name = fx.memberStr(f, "name").?;

        var applies = false;
        for (fx.member(f, "profiles").?.array.items) |p| {
            if (oneOf(p.string, &cli_profiles)) applies = true;
        }
        if (!applies) {
            w.skipped += 1; // the fixture's profiles exclude console/all
            continue;
        }
        w.walked += 1;

        // Verdict precedence: local override ?? knownDivergence.cli ?? expect.
        var want = fx.memberStr(f, "expect").?;
        if (fx.member(f, "knownDivergence")) |kd| {
            if (fx.memberStr(kd, "cli")) |v| want = v;
        }
        if (w.override("opPlan", name)) |ov| {
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
        if (!std.mem.eql(u8, got, want))
            w.fail("opPlan {s}: want {s}, cli says {s} {s}\n", .{ file, want, got, detail });
    }
    try w.report("opPlan");
}

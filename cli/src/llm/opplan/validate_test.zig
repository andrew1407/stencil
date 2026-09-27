//! parsePlan end to end: core's walk, the typed `Action` mapping and core's canonical §1 wording.
//! The key-spec rules themselves are proved in core (core/tests/opplan) against the shared corpus.
const std = @import("std");
const testing = std.testing;
const validate = @import("validate.zig");
const registry = @import("../registry.zig");
const mustPlan = validate.mustPlan;
const mustReject = validate.mustReject;
const planFor = validate.planFor;
const rejectFor = validate.rejectFor;
const planInVariant = validate.planInVariant;

test "parsePlan: chat-only fallback (no JSON object, or braces that are not JSON)" {
    var p1 = try mustPlan("  Just chatting, no ops needed.  ");
    defer p1.deinit();
    try testing.expect(p1.chat_only);
    try testing.expectEqualStrings("Just chatting, no ops needed.", p1.reply);
    try testing.expectEqual(@as(usize, 0), p1.actions.len);
    inline for (.{ "some text with {braces that are not json", "{ not: json }" }) |raw| {
        var p = try mustPlan(raw);
        defer p.deinit();
        try testing.expect(p.chat_only);
    }
}

test "parsePlan: fence stripping + first balanced object + surrounding prose" {
    var p = try mustPlan("Sure! Here is the plan:\n```json\n{\"version\":1,\"reply\":\"rotating\",\"actions\":[{\"op\":\"rotate\",\"dir\":\"left\"}]}\n```\nAnything else?");
    defer p.deinit();
    try testing.expect(!p.chat_only);
    try testing.expectEqualStrings("rotating", p.reply);
    try testing.expect(p.actions[0].rotate.dir == .left);
    try testing.expectEqual(@as(u8, 1), p.actions[0].rotate.times); // default
    var p2 = try mustPlan("{\"version\":2,\"reply\":\"ok {see}\",\"actions\":[]}");
    defer p2.deinit();
    try testing.expectEqualStrings("ok {see}", p2.reply); // braces in strings; version 2 ignored
}

test "parsePlan: unknown ops warn, bad params reject, a missing reply is substituted" {
    var p = try planFor("{\"op\":\"resize\",\"w\":100},{\"op\":\"filter\",\"mode\":\"bw\"}");
    defer p.deinit();
    try testing.expectEqual(@as(usize, 1), p.actions.len);
    try testing.expectEqualStrings("Skipped unknown operation \"resize\"", p.warnings[0]);
    try rejectFor("{\"op\":\"rotate\",\"dir\":\"left\",\"times\":5}", "Invalid rotate action: \"times\" must be an integer 1..3");
    try rejectFor("{\"op\":\"rotate\",\"dir\":\"left\",\"angle\":45}", "Invalid rotate action: unknown field \"angle\"");
    try rejectFor("{\"op\":\"crop\",\"spec\":{\"album\":true}}", "Invalid crop action: spec needs at least one of x1/x2/y1/y2/aspect");
    try rejectFor("{\"op\":\"openFile\",\"path\":\"~/Documents\"}", "Invalid openFile action: \"~/Documents\" is not an image, video, .json layout or .stencil project");
    var tolerated = try mustPlan("{\"actions\":[{\"op\":\"rotate\",\"dir\":\"left\"}]}");
    defer tolerated.deinit();
    try testing.expectEqualStrings("Done.", tolerated.reply);
    try testing.expectEqualStrings("The model omitted its reply — the plan still ran", tolerated.warnings[0]);
    var blank = try mustPlan("{\"reply\":\"  \"}");
    defer blank.deinit();
    try testing.expectEqualStrings("The model returned an empty plan — nothing was changed.", blank.reply);
}

test "wording: every envelope failure is core's canonical message" {
    const rot = "{\"op\":\"rotate\",\"dir\":\"left\"}";
    const cases = [_][2][]const u8{
        .{ "{\"reply\":\"ok\",\"actions\":{}}", "Invalid plan: \"actions\" must be an array" },
        .{ "{\"reply\":\"ok\",\"actions\":[" ++ ((rot ++ ",") ** 16) ++ rot ++ "]}", "Invalid plan: more than 16 actions in \"actions\"" },
        .{ "{\"reply\":\"ok\",\"actions\":[5]}", "Invalid plan: every action in \"actions\" must be an object with an \"op\"" },
        .{ "{\"reply\":\"ok\",\"actions\":[{\"x\":1}]}", "Invalid plan: every action in \"actions\" must be an object with an \"op\"" },
        .{ "{\"reply\":\"ok\",\"variants\":{}}", "Invalid plan: \"variants\" must be an array" },
        .{ "{\"reply\":\"ok\",\"variants\":[" ++ ("{\"actions\":[]}," ** 8) ++ "{\"actions\":[]}]}", "Invalid plan: more than 8 variants" },
        .{ "{\"reply\":\"ok\",\"variants\":[5]}", "Invalid plan: every variant must be an object" },
        .{ "{\"reply\":\"ok\",\"variants\":[{\"label\":7}]}", "Invalid plan: variant \"label\" must be a string" },
        .{ "{\"reply\":\"ok\",\"variants\":[{\"label\":\"" ++ ("x" ** 5001) ++ "\"}]}", "Invalid plan: variant \"label\" must be a string" },
        .{ "{\"reply\":\"ok\",\"variants\":[{\"actions\":{}}]}", "Invalid plan: variant 1 must be an array" },
        .{ "{\"reply\":\"ok\",\"variants\":[{\"actions\":[5]}]}", "Invalid plan: every action in variant 1 must be an object with an \"op\"" },
        .{ "{\"reply\":\"ok\",\"variants\":[{\"actions\":[{\"x\":1}]}]}", "Invalid plan: every action in variant 1 must be an object with an \"op\"" },
        .{ "{\"reply\":\"ok\",\"variants\":[{\"actions\":[" ++ ((rot ++ ",") ** 16) ++ rot ++ "]}]}", "Invalid plan: more than 16 actions in variant 1" },
        .{ "{\"reply\":\"ok\",\"ask\":{\"question\":\"Q\",\"options\":[{\"label\":\"A\",\"actions\":[{\"x\":1}]},{\"label\":\"B\"}]}}", "Invalid plan: every action in ask option 1 must be an object with an \"op\"" },
        .{ "{\"reply\":\"ok\",\"actions\":[{\"op\":\"llm\"}]}", "Invalid plan: the \"llm\" op is never model-drivable" },
        .{ "{\"reply\":\"ok\",\"ask\":\"q\"}", "Invalid plan: \"ask\" must be an object" },
    };
    for (cases) |c| try mustReject(c[0], c[1]);
}

test "wording: core's warnings in its words and order, then the console's previews note" {
    var p = try mustPlan("{\"actions\":[{\"op\":\"rotate\",\"dir\":\"left\"}],\"variants\":[{\"label\":\"W\",\"actions\":[{\"op\":\"clear\"}]}," ++
        "{\"actions\":[{\"op\":\"undo\"}]}],\"ask\":{\"question\":\"Q\",\"options\":[{\"label\":\"A\",\"actions\":[{\"op\":\"zz\"},{\"op\":\"clear\"}]}," ++
        "{\"label\":\"B\",\"image\":{\"scanIndex\":1}}]}}");
    defer p.deinit();
    const want = [_][]const u8{
        "Dropped variant 1 (\"W\") — editor-settings op \"clear\" is not allowed inside variants; the rest of the plan ran",
        "Dropped variant 2 (\"variant 2\") — \"undo\" steps the live edit history — a top-level action only, not allowed inside variants or previews; the rest of the plan ran",
        "Skipped unknown operation \"zz\"",
        "Dropped the preview for ask option 1 (\"A\") — editor-settings op \"clear\" is not allowed inside variants or previews; the option is still offered",
        "The model omitted its reply — the plan still ran",
        validate.previews_note,
    };
    try testing.expectEqual(want.len, p.warnings.len);
    for (want, p.warnings) |w, got| try testing.expectEqualStrings(w, got);
    try testing.expectEqual(@as(usize, 0), p.variants.len);
    try testing.expectEqual(@as(usize, 2), p.ask.?.options.len); // an option is never dropped
}

test "typed: crop edges, the folded aspect and the console album key" {
    var p = try planFor("{\"op\":\"crop\",\"spec\":{\"x1\":\"10%\",\"x2\":\"-1.5cm\",\"album\":true}},{\"op\":\"crop\",\"spec\":{\"x1\":\"10%\"},\"aspect\":\"3:4\"}");
    defer p.deinit();
    try testing.expectEqualStrings("10%", p.actions[0].crop.x1.?);
    try testing.expectEqualStrings("-1.5cm", p.actions[0].crop.x2.?);
    try testing.expect(p.actions[0].crop.album and p.actions[0].crop.y1 == null);
    try testing.expectEqualStrings("3:4", p.actions[1].crop.aspect.?);
    try testing.expect(!p.actions[1].crop.album);
}

test "typed: filter, formula, page, blank and frame" {
    var p = try planFor("{\"op\":\"filter\",\"mode\":\"custom\",\"tint\":\"#A1b2c3\"},{\"op\":\"formula\",\"axis\":\"y\",\"expr\":\"  \"}," ++
        "{\"op\":\"formula\",\"enabled\":false},{\"op\":\"page\",\"width\":21,\"height\":29.7},{\"op\":\"blank\",\"color\":\"pink\",\"format\":\"a4\",\"width\":10,\"height\":5}," ++
        "{\"op\":\"frame\",\"index\":3}");
    defer p.deinit();
    try testing.expectEqualStrings("#A1b2c3", p.actions[0].filter.tint);
    try testing.expectEqual(@as(u8, 'y'), p.actions[1].formula.axis);
    try testing.expectEqualStrings("", p.actions[1].formula.expr); // blank clears the axis
    try testing.expectEqual(@as(?bool, false), p.actions[2].formula.enabled);
    try testing.expectEqualStrings("", p.actions[3].page.format);
    try testing.expectEqual(@as(f64, 29.7), p.actions[3].page.height);
    try testing.expectEqualStrings("a4", p.actions[4].blank.format.?);
    try testing.expectEqual(@as(f64, 10), p.actions[4].blank.width);
    try testing.expectEqualSlices(u32, &.{3}, p.actions[5].frame.indices);
    try testing.expect(p.hasFrameOp());
}

test "typed: layout lines re-serialize as the document /apply consumes" {
    var p = try mustPlan("{\"reply\":\"drawn\",\"actions\":[{\"op\":\"layout\",\"lines\":[{\"points\":[{\"x\":1,\"y\":2},{\"x\":3,\"y\":4}],\"color\":\"#FF0000\",\"style\":\"dashed\",\"locked\":true}]}]}");
    defer p.deinit();
    try testing.expectEqualStrings(
        "[{\"points\":[{\"x\":1,\"y\":2},{\"x\":3,\"y\":4}],\"color\":\"#FF0000\",\"style\":\"dashed\",\"locked\":true}]",
        p.actions[0].layout.lines_json,
    );
    try rejectFor("{\"op\":\"layout\",\"lines\":[{\"points\":[{\"x\":1,\"z\":2}]}]}", "Invalid layout action: unknown field \"z\" in lines[0].points[0]");
}

test "typed: image, save, history and the console's own ops" {
    var p = try planFor("{\"op\":\"image\",\"index\":2},{\"op\":\"save\",\"name\":\"portrait 1\"},{\"op\":\"save\",\"path\":\" ~/Downloads \"}," ++
        "{\"op\":\"undo\"},{\"op\":\"redo\",\"steps\":3},{\"op\":\"reset\"},{\"op\":\"accent\",\"preset\":\" green \"}," ++
        "{\"op\":\"connect\",\"server\":\" a \"},{\"op\":\"delete\",\"path\":\"old.stencil\"},{\"op\":\"openFile\",\"path\":\"/tmp/x.WEBP\"}," ++
        "{\"op\":\"openUrl\",\"url\":\" https://a.example/cat.png \",\"incognito\":true},{\"op\":\"copy\"},{\"op\":\"clear\"},{\"op\":\"clearChat\"}");
    defer p.deinit();
    try testing.expectEqual(@as(u32, 2), p.actions[0].image.index);
    try testing.expectEqualStrings("portrait 1", p.actions[1].save.name);
    try testing.expectEqualStrings("~/Downloads", p.actions[2].save.path); // trimmed
    try testing.expectEqual(@as(u8, 1), p.actions[3].undo.steps);
    try testing.expectEqual(@as(u8, 3), p.actions[4].redo.steps);
    try testing.expect(p.actions[5] == .reset);
    try testing.expectEqualStrings("green", p.actions[6].accent.preset);
    try testing.expectEqualStrings("a", p.actions[7].connect.server);
    try testing.expectEqualStrings("old.stencil", p.actions[8].delete.path);
    try testing.expectEqualStrings("/tmp/x.WEBP", p.actions[9].open_file.path);
    try testing.expectEqualStrings("https://a.example/cat.png", p.actions[10].open_url.url);
    try testing.expect(p.actions[10].open_url.incognito);
    try testing.expect(p.actions[11] == .copy and p.actions[12] == .clear and p.actions[13] == .clear_chat);
    var gui = try planFor("{\"op\":\"theme\",\"mode\":\"dark\"},{\"op\":\"filter\",\"mode\":\"bw\"}");
    defer gui.deinit();
    try testing.expectEqualStrings("Skipped unknown operation \"theme\"", gui.warnings[0]); // the GUI's §10 op
}

test "§13 forbidden ops: never registered, and a plan naming one is rejected outright" {
    for (registry.op_registry) |d| try testing.expect(!registry.isForbiddenOp(d.name));
    try testing.expect(registry.isForbiddenOp("llm") and registry.isForbiddenOp("paste") and registry.isForbiddenOp("quit"));
    try validate.rejectInVariant("{\"op\":\"exit\"}", "Invalid plan: the \"exit\" op is never model-drivable");
    var unknown = try planInVariant("{\"op\":\"resize\",\"w\":2},{\"op\":\"filter\",\"mode\":\"bw\"}");
    defer unknown.deinit();
    try testing.expectEqual(@as(usize, 1), unknown.variants.len);
}

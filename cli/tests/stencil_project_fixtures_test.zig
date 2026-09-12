// Walks the shared `.stencil` project-file vectors (fixtures/stencilProject/) against
// the cli's real parser (project.parse — the loadInto/one-shot path). Verdicts match on
// the error CASE, not the browser's message text (per the schema). The fixture `project`
// is the BROWSER-normalized shape; the cli-observable fields (name/color/source/
// resource/blank/blankColor/ext/w/h + the decoded image bytes) are compared, with the
// cli's own value pinned via fixture_overrides.json `cli` where the two disagree
// (the cli keeps metadata verbatim — no trim/lowercase/hex-normalization — and its
// typed JSON reader neither coerces numeric strings nor truthy non-bools).
// The embedded layout is NOT compared here: the cli re-stringifies it raw at parse and
// sanitizes only later through layout.zig (walked by the layout family).
const std = @import("std");
const project = @import("../src/project.zig");
const fx = @import("fixture_corpus.zig");
const testing = std.testing;

/// The base64 payload of the fixture's dataUrl, decoded the same way the format
/// documents it — what the cli's image_bytes must equal on an "ok" parse.
fn decodePayload(a: std.mem.Allocator, data_url: []const u8) ![]u8 {
    const idx = std.mem.indexOf(u8, data_url, "base64,").?;
    const b64 = std.mem.trim(u8, data_url[idx + "base64,".len ..], " \t\r\n");
    const out = try a.alloc(u8, try std.base64.standard.Decoder.calcSizeForSlice(b64));
    try std.base64.standard.Decoder.decode(out, b64);
    return out;
}

/// The expected value for one cli-observable string field: the override's `cli`
/// replacement when recorded, else the corpus value.
fn wantStr(proj_fx: std.json.Value, ov: ?std.json.Value, key: []const u8, default: []const u8) []const u8 {
    if (ov) |o| {
        if (fx.member(o, "cli")) |c| {
            if (fx.memberStr(c, key)) |v| return v;
        }
    }
    return fx.memberStr(proj_fx, key) orelse default;
}

fn walkFile(w: *fx.Walk, sub: []const u8) !void {
    const a = w.alloc();
    for (try w.cases(sub)) |case| {
        w.walked += 1;
        const name = fx.memberStr(case, "name").?;
        const ov = w.override("stencilProject", name);
        var want = fx.memberStr(case, "expect").?;
        if (ov) |o| {
            if (fx.memberStr(o, "verdict")) |v| want = v;
        }
        const file = fx.memberStr(case, "file").?;

        var parsed = project.parse(testing.allocator, file) catch |e| {
            if (!std.mem.eql(u8, want, "error"))
                w.fail("stencilProject '{s}': want ok, cli rejects with {s}\n", .{ name, @errorName(e) });
            continue;
        };
        defer parsed.deinit();
        if (std.mem.eql(u8, want, "error")) {
            w.fail("stencilProject '{s}': want error, cli parsed ok\n", .{name});
            continue;
        }

        // Field comparison only where the corpus provides the normalized project
        // (a verdict override to "ok" has no corpus shape to compare against).
        const proj_fx = fx.member(case, "project") orelse continue;
        const img_fx = fx.member(proj_fx, "image").?;
        try testing.expectEqualStrings(wantStr(proj_fx, ov, "name", "Untitled"), parsed.name);
        try testing.expectEqualStrings(wantStr(proj_fx, ov, "color", ""), parsed.color);
        try testing.expectEqualStrings(wantStr(proj_fx, ov, "source", ""), parsed.source);
        try testing.expectEqualStrings(wantStr(proj_fx, ov, "resource", ""), parsed.resource);
        try testing.expectEqualStrings(wantStr(proj_fx, ov, "blankColor", ""), parsed.blank_color);
        try testing.expectEqualStrings(wantStr(img_fx, ov, "ext", "png"), parsed.image_ext);

        var want_blank = fx.member(proj_fx, "blank").?.bool;
        var want_w: i64 = if (fx.member(img_fx, "w")) |v| v.integer else 0;
        var want_h: i64 = if (fx.member(img_fx, "h")) |v| v.integer else 0;
        if (ov) |o| {
            if (fx.member(o, "cli")) |c| {
                if (fx.member(c, "blank")) |b| want_blank = b.bool;
                if (fx.member(c, "w")) |v| want_w = v.integer;
                if (fx.member(c, "h")) |v| want_h = v.integer;
            }
        }
        try testing.expectEqual(want_blank, parsed.blank);
        try testing.expectEqual(want_w, parsed.image_w);
        try testing.expectEqual(want_h, parsed.image_h);

        // The embedded image decodes to exactly the dataUrl's base64 payload.
        const bytes = try decodePayload(a, fx.memberStr(img_fx, "dataUrl").?);
        try testing.expectEqualSlices(u8, bytes, parsed.image_bytes);
    }
}

test "stencilProject corpus: valid.json + invalid.json against project.parse" {
    var w = fx.Walk.start();
    defer w.stop();
    try w.loadOverrides();
    try walkFile(&w, "fixtures/stencilProject/valid.json");
    try walkFile(&w, "fixtures/stencilProject/invalid.json");
    try w.report("stencilProject");
}

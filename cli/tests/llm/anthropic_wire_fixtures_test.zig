//! Walks the shared §6.5 corpus (common/fixtures/llm/providerWire/anthropic.json) through the cli's real
//! client: buildRequestWithSystem, then postJsonVia over a capturing exchange that answers with the
//! case's response, then extractReply — so the URL, the headers (the browser-only one ABSENT),
//! the body, the typed errors and the reason printed are what would really leave and land. cli
//! shape, pinned in fixture_overrides.json: a replayed turn is text-only, so its images drop.
const std = @import("std");
const llm = @import("../../src/llm.zig");
const net = @import("../../src/net.zig");
const report = @import("../../src/app/report.zig");
const fx = @import("../fixture_corpus.zig");
const testing = std.testing;

const corpus = "llm/providerWire/anthropic.json";

/// The capturing exchange: counts the sends, keeps what the last one carried, answers as told.
const Exchange = struct {
    var arena: std.mem.Allocator = undefined;
    var sends: usize = 0;
    var url: []const u8 = "";
    var body: []const u8 = "";
    var headers: std.ArrayList(std.http.Header) = .empty;
    var status: u16 = 200;
    var answer: []const u8 = "";

    fn send(gpa: std.mem.Allocator, _: std.Io, u: []const u8, hs: []const std.http.Header, b: []const u8) llm.PostError!net.Response {
        sends += 1;
        url = arena.dupe(u8, u) catch return error.OutOfMemory;
        body = arena.dupe(u8, b) catch return error.OutOfMemory;
        headers.clearRetainingCapacity();
        for (hs) |h| headers.append(arena, .{ .name = arena.dupe(u8, h.name) catch return error.OutOfMemory, .value = arena.dupe(u8, h.value) catch return error.OutOfMemory }) catch return error.OutOfMemory;
        return .{ .status = status, .body = gpa.dupe(u8, answer) catch return error.OutOfMemory };
    }

    fn header(name: []const u8) ?[]const u8 {
        for (headers.items) |h| if (std.ascii.eqlIgnoreCase(h.name, name)) return h.value;
        return null;
    }
};

/// What the transport reported, the `error:` lines only, without their prefix.
const Said = struct {
    var errs: std.ArrayList(u8) = .empty;
    var arena: std.mem.Allocator = undefined;
    fn emit(_: *anyopaque, sev: report.Severity, text: []const u8) void {
        if (sev == .err) errs.appendSlice(arena, text) catch {};
    }
};

fn failedWith(r: llm.PostError![]u8, want: llm.PostError) bool {
    return if (r) |_| false else |e| e == want;
}

/// Drop the image blocks of every replayed (non-final) message: the cli replays text only.
fn dropHistoryImages(a: std.mem.Allocator, expect: std.json.Value) !void {
    const msgs = fx.member(expect, "messages").?.array.items;
    for (msgs[0 .. msgs.len - 1]) |m| {
        const content = m.object.getPtr("content").?;
        var kept = std.json.Array.init(a);
        for (content.array.items) |part| if (std.mem.eql(u8, fx.memberStr(part, "type").?, "text")) try kept.append(part);
        content.* = .{ .array = kept };
    }
}

test "providerWire corpus: the anthropic wire, request to printed reason, over a capturing exchange" {
    var w = fx.Walk.start();
    defer w.stop();
    const a = w.alloc();
    const io = w.io();
    try w.loadOverrides();
    Exchange.arena = a;
    Said.arena = a;
    var dummy: u8 = 0;
    report.install(.{ .ctx = &dummy, .emitFn = Said.emit });
    defer {
        report.uninstall();
        Said.errs = .empty; // arena-owned, gone with the walk
        Exchange.headers = .empty;
    }

    const cases = try w.cases(corpus);
    for (cases) |case| {
        w.walked += 1;
        const name = fx.memberStr(case, "name").?;
        try testing.expectEqualStrings("anthropic", fx.memberStr(case, "provider").?);
        const settings = fx.member(case, "settings").?;
        var cfg = llm.Config{
            .provider = .anthropic,
            .base_url = try a.dupe(u8, fx.memberStr(settings, "baseUrl").?),
            .model = try a.dupe(u8, fx.memberStr(settings, "model") orelse ""),
            .api_key = try a.dupe(u8, fx.memberStr(settings, "apiKey") orelse ""),
        };
        const chat = fx.member(case, "chat").?;
        const msgs = fx.member(chat, "messages").?.array.items;
        var history: std.ArrayList(llm.Turn) = .empty;
        for (msgs[0 .. msgs.len - 1]) |m| try history.append(a, .{
            .role = std.meta.stringToEnum(llm.ChatRole, fx.memberStr(m, "role").?).?,
            .text = fx.memberStr(m, "text").?,
        });
        const last = msgs[msgs.len - 1];
        var images: std.ArrayList([]const u8) = .empty;
        if (fx.member(last, "images")) |imgs| for (imgs.array.items) |img| try images.append(a, fx.memberStr(img, "data").?);
        var req = try llm.buildRequestWithSystem(a, &cfg, fx.memberStr(chat, "system").?, fx.memberStr(last, "text").?, images.items, "", "", history.items, "");
        defer req.deinit(a);

        Exchange.sends = 0;
        Said.errs.clearRetainingCapacity();
        const expect_err = fx.member(case, "expectError");
        if (fx.member(case, "response")) |resp| {
            Exchange.status = 200;
            Exchange.answer = try fx.stringify(a, resp);
        } else if (fx.member(case, "errorResponse")) |er| {
            Exchange.status = @intCast(fx.member(er, "status").?.integer);
            const b = fx.member(er, "body").?;
            Exchange.answer = if (b == .string) b.string else try fx.stringify(a, b);
        }
        const posted = llm.postJsonVia(a, io, &req, null, Exchange.send);

        if (fx.member(case, "expectNoRequest") != null) {
            // No key: nothing sent, the typed disabled error, its exact words.
            if (Exchange.sends != 0 or !failedWith(posted, error.LlmDisabled))
                w.fail("providerWire '{s}': a keyless turn must send nothing and fail disabled\n", .{name});
            try testing.expectEqualStrings("disabled", fx.memberStr(expect_err.?, "kind").?);
            try testing.expectEqualStrings(fx.memberStr(expect_err.?, "message").?, std.mem.trimEnd(u8, Said.errs.items, "\n"));
            continue;
        }

        // What left: one POST to the URL, the §6.5 headers and no others, the body.
        try testing.expectEqual(@as(usize, 1), Exchange.sends);
        try testing.expectEqualStrings(fx.memberStr(case, "expectUrl").?, Exchange.url);
        var it = fx.member(case, "expectHeaders").?.object.iterator();
        while (it.next()) |h| {
            const got = Exchange.header(h.key_ptr.*) orelse "";
            if (!std.mem.eql(u8, got, h.value_ptr.string))
                w.fail("providerWire '{s}': header {s} = '{s}', want '{s}'\n", .{ name, h.key_ptr.*, got, h.value_ptr.string });
        }
        try testing.expectEqualStrings("application/json", Exchange.header("content-type").?);
        try testing.expect(fx.member(case, "expectAuthorization") == null and Exchange.header("authorization") == null);
        try testing.expect(Exchange.header("anthropic-dangerous-direct-browser-access") == null); // not a browser
        const expect_body = fx.member(case, "expectBody").?;
        if (w.override("providerWire", name)) |ov| if (fx.member(ov, "dropHistoryImages") != null) try dropHistoryImages(a, expect_body);
        const got_body = try std.json.parseFromSliceLeaky(std.json.Value, a, Exchange.body, .{});
        if (!fx.jsonEquals(expect_body, got_body))
            w.fail("providerWire '{s}': body mismatch\n  want {s}\n  got  {s}\n", .{ name, try fx.stringify(a, expect_body), Exchange.body });

        // What landed: a reply, or the typed error with the reason printed once.
        if (fx.member(case, "errorResponse") != null) {
            try testing.expectEqualStrings("http", fx.memberStr(expect_err.?, "kind").?);
            if (!failedWith(posted, error.HttpFailed)) w.fail("providerWire '{s}': want HttpFailed\n", .{name});
            const said = std.mem.trimEnd(u8, Said.errs.items, "\n");
            if (!std.mem.eql(u8, said, fx.memberStr(expect_err.?, "message").?))
                w.fail("providerWire '{s}': reason\n  want: {s}\n  cli:  {s}\n", .{ name, fx.memberStr(expect_err.?, "message").?, said });
            continue;
        }
        const ex = try llm.extractReply(a, .anthropic, try posted);
        const ok = if (fx.memberStr(case, "expectReply")) |want| ex == .text and std.mem.eql(u8, ex.text, want) else blk: {
            const kind = fx.memberStr(expect_err.?, "kind").?;
            if (std.mem.eql(u8, kind, "truncated")) break :blk ex == .truncated;
            if (std.mem.eql(u8, kind, "badReply")) break :blk ex == .bad_reply;
            break :blk ex == .refusal and std.mem.eql(u8, ex.refusal, fx.memberStr(expect_err.?, "message").?);
        };
        if (!ok) w.fail("providerWire '{s}': extraction gave {s}\n", .{ name, @tagName(ex) });
    }
    try testing.expectEqual(cases.len, w.walked);
    try w.report("providerWire anthropic");
}

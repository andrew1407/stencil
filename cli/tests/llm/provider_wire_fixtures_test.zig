// Walks the shared provider-wire corpus (llm/fixtures/providerWire/) against the cli's real request
// builder + reply extraction (llm.buildRequestWithSystem / llm.extractReply / llm.errorDetail). The
// request body is compared to expectBody STRUCTURALLY, so field ABSENCE is part of the contract. cli
// shape, pinned via fixture_overrides.json: history turns are text-only (§7/§12), every attachment is
// sent as image/png, an off-shape 2xx is a typed `bad_reply`, and a 503 llmDisabled types its own.
const std = @import("std");
const llm = @import("../../src/llm.zig");
const fx = @import("../fixture_corpus.zig");
const testing = std.testing;

const wire_files = [_][]const u8{
    "llm/fixtures/providerWire/ollama.json",
    "llm/fixtures/providerWire/openai.json",
    "llm/fixtures/providerWire/server.json",
    "llm/fixtures/providerWire/httpErrors.json",
};

fn providerOf(s: []const u8) llm.Provider {
    if (std.mem.eql(u8, s, "ollama")) return .ollama;
    if (std.mem.eql(u8, s, "openai")) return .openai_compat;
    return .stencil_server;
}

/// Strip "images" from every non-final expectBody message (cli history is text-only).
fn dropHistoryImages(body: std.json.Value) void {
    const msgs = fx.member(body, "messages") orelse return;
    if (msgs != .array or msgs.array.items.len == 0) return;
    for (msgs.array.items[0 .. msgs.array.items.len - 1]) |*m| {
        if (m.* == .object) _ = m.object.orderedRemove("images");
    }
}

/// Rewrite every data-URL mime in expectBody to image/png (the cli's fixed mediaType).
fn forcePngDataUrls(a: std.mem.Allocator, body: std.json.Value) !void {
    const msgs = fx.member(body, "messages") orelse return;
    if (msgs != .array) return;
    for (msgs.array.items) |m| {
        const content = fx.member(m, "content") orelse continue;
        if (content != .array) continue;
        for (content.array.items) |part| {
            if (part != .object) continue;
            const iu = part.object.getPtr("image_url") orelse continue;
            if (iu.* != .object) continue;
            const url = iu.object.getPtr("url") orelse continue;
            if (url.* != .string) continue;
            const at = std.mem.indexOf(u8, url.string, ";base64,") orelse continue;
            url.* = .{ .string = try std.fmt.allocPrint(a, "data:image/png{s}", .{url.string[at..]}) };
        }
    }
}

test "providerWire corpus: request building + reply extraction against the cli client" {
    var w = fx.Walk.start();
    defer w.stop();
    const a = w.alloc();

    try w.loadOverrides();
    for (wire_files) |file| {
        for (try w.cases(file)) |case| {
            w.walked += 1;
            const name = fx.memberStr(case, "name").?;
            const ov = w.override("providerWire", name);
            const provider = providerOf(fx.memberStr(case, "provider").?);
            const settings = fx.member(case, "settings").?;

            var cfg = llm.Config{
                .provider = provider,
                .base_url = try a.dupe(u8, fx.memberStr(settings, "baseUrl") orelse ""),
                .model = try a.dupe(u8, fx.memberStr(settings, "model") orelse ""),
                .api_key = try a.dupe(u8, fx.memberStr(settings, "apiKey") orelse ""),
            };
            const server_url = fx.memberStr(settings, "serverUrl") orelse "";
            const token = fx.memberStr(case, "token") orelse "";

            // The canonical chat: leading turns replay as text-only history, the final
            // (user) turn is the prompt, its attachments the images.
            const chat = fx.member(case, "chat").?;
            const msgs = fx.member(chat, "messages").?.array.items;
            var history: std.ArrayList(llm.Turn) = .empty;
            for (msgs[0 .. msgs.len - 1]) |m| {
                try history.append(a, .{
                    .role = std.meta.stringToEnum(llm.ChatRole, fx.memberStr(m, "role").?).?,
                    .text = fx.memberStr(m, "text").?,
                });
            }
            const last = msgs[msgs.len - 1];
            var images: std.ArrayList([]const u8) = .empty;
            if (fx.member(last, "images")) |imgs| {
                for (imgs.array.items) |img| try images.append(a, fx.memberStr(img, "data").?);
            }

            const req = try llm.buildRequestWithSystem(
                a,
                &cfg,
                fx.memberStr(chat, "system").?,
                fx.memberStr(last, "text").?,
                images.items,
                server_url,
                token,
                history.items,
                "",
            );

            try testing.expectEqualStrings(fx.memberStr(case, "expectUrl").?, req.url);
            if (fx.memberStr(case, "expectAuthorization")) |auth| {
                try testing.expectEqualStrings(auth, req.auth.?);
            } else {
                try testing.expect(req.auth == null); // absence IS the contract
            }

            // Deep-compare the body; measured cli shape divergences transform the
            // expectation per the recorded override, never the fixture.
            const expect_body = fx.member(case, "expectBody").?;
            if (ov) |o| {
                if (fx.member(o, "dropHistoryImages") != null) dropHistoryImages(expect_body);
                if (fx.member(o, "mediaTypeAlwaysPng") != null) try forcePngDataUrls(a, expect_body);
            }
            const got_body = try std.json.parseFromSliceLeaky(std.json.Value, a, req.body, .{});
            if (!fx.jsonEquals(expect_body, got_body))
                w.fail("providerWire '{s}': body mismatch\n  want {s}\n  got  {s}\n", .{ name, try fx.stringify(a, expect_body), req.body });

            // Feed the canned 2xx response through reply extraction.
            if (fx.member(case, "response")) |resp| {
                const rbytes = try fx.stringify(a, resp);
                const ex = try llm.extractReply(a, provider, rbytes);
                const want_kind: []const u8 = if (ov != null and fx.memberStr(ov.?, "extract") != null)
                    fx.memberStr(ov.?, "extract").?
                else if (fx.member(case, "expectReply") != null)
                    "text"
                else
                    fx.memberStr(fx.member(case, "expectError").?, "kind").?;
                const ok = blk: {
                    if (std.mem.eql(u8, want_kind, "text"))
                        break :blk ex == .text and std.mem.eql(u8, ex.text, fx.memberStr(case, "expectReply").?);
                    if (std.mem.eql(u8, want_kind, "badReply")) break :blk ex == .bad_reply;
                    if (std.mem.eql(u8, want_kind, "truncated")) break :blk ex == .truncated;
                    if (std.mem.eql(u8, want_kind, "refusal"))
                        break :blk ex == .refusal and std.mem.eql(u8, ex.refusal, fx.memberStr(fx.member(case, "expectError").?, "message").?);
                    break :blk false;
                };
                if (!ok)
                    w.fail("providerWire '{s}': extraction mismatch (want {s}, got {s})\n", .{ name, want_kind, @tagName(ex) });
            }

            // Non-2xx: the printed reason is errorDetail's output, and expectError.message is post-browser-sanitizer
            // text. A message of exactly "HTTP <status>" is the browser's empty-detail fallback (cli: detail == "").
            if (fx.member(case, "errorResponse")) |er| {
                const expect_err = fx.member(case, "expectError").?;
                const status = fx.member(er, "status").?.integer;
                const body_v = fx.member(er, "body").?;
                const body_bytes = if (body_v == .string) body_v.string else try fx.stringify(a, body_v);
                var dbuf: [200 + "…".len]u8 = undefined;
                const got = llm.errorDetail(a, body_bytes, &dbuf);

                var want = fx.memberStr(expect_err, "message").?;
                if (ov != null and fx.memberStr(ov.?, "detail") != null) want = fx.memberStr(ov.?, "detail").?;
                const fallback = try std.fmt.allocPrint(a, "HTTP {d}", .{status});
                if (std.mem.eql(u8, want, fallback)) want = "";
                if (!std.mem.eql(u8, got, want))
                    w.fail("providerWire '{s}': error detail mismatch\n  want: {s}\n  cli:  {s}\n", .{ name, want, got });
                // expectError.status always matches the transport status finish() branches on; kind llmDisabled types
                // as the dedicated LlmDisabled error (finish() keys on isLlmDisabled), everything else HttpFailed.
                try testing.expectEqual(status, fx.member(expect_err, "status").?.integer);
                const want_disabled = std.mem.eql(u8, fx.memberStr(expect_err, "kind").?, "disabled");
                try testing.expectEqual(want_disabled, llm.isLlmDisabled(a, body_bytes));
            }
        }
    }
    try w.report("providerWire");
}

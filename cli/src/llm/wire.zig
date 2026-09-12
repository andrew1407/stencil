//! Wire layer for the console LLM assistant: chat history + the §12.1 persisted
//! chat document, the three provider request mappings (§6), and reply extraction
//! (incl. the stencil-server stopReason contract).
const std = @import("std");
const config = @import("config.zig");
const registry = @import("registry.zig");
const transport = @import("transport.zig");

const json = @import("wire/json.zig");
const chatDoc = @import("wire/chatDoc.zig");
const request = @import("wire/request.zig");
const body = @import("wire/body.zig");
const reply = @import("wire/reply.zig");

const Config = config.Config;
const clip_limit = transport.clip_limit;
const edge_map_suffix = registry.edge_map_suffix;
const systemPrompt = registry.systemPrompt;

pub const member = json.member;
pub const memberStr = json.memberStr;

pub const max_chat_messages = chatDoc.max_chat_messages;
pub const ChatRole = chatDoc.ChatRole;
pub const Turn = chatDoc.Turn;
pub const continuation_note = chatDoc.continuation_note;
pub const chatDisplayText = chatDoc.chatDisplayText;
pub const chatDocAlloc = chatDoc.chatDocAlloc;
pub const parseChatDoc = chatDoc.parseChatDoc;
pub const freeTurns = chatDoc.freeTurns;

pub const Request = request.Request;
pub const buildRequest = request.buildRequest;
pub const buildRequestWithHistory = request.buildRequestWithHistory;
pub const buildRequestWithSystem = request.buildRequestWithSystem;

pub const Extracted = reply.Extracted;
pub const extractReply = reply.extractReply;

const testing = std.testing;

test "buildRequest: the canonical system prompt, its §7 suffix, and empty-history equivalence" {
    // The §6 mappings (url, auth, body shape, history and image order) are walked from the
    // shared corpus by tests/provider_wire_fixtures_test.zig; only what no corpus case can
    // reach is asserted here.
    const a = testing.allocator;
    var cfg = try Config.init(a, .{ .model = "llava" });
    defer cfg.deinit(a);

    const sys = try std.json.Stringify.valueAlloc(a, @as([]const u8, systemPrompt()), .{});
    defer a.free(sys);
    var plain = try buildRequest(a, &cfg, "hi", &.{}, "", "");
    defer plain.deinit(a);
    try testing.expect(std.mem.indexOf(u8, plain.body, sys) != null);

    const joined = try std.mem.join(a, "\n\n", &.{ systemPrompt(), edge_map_suffix });
    defer a.free(joined);
    const suffixed = try std.json.Stringify.valueAlloc(a, @as([]const u8, joined), .{});
    defer a.free(suffixed);
    var edged = try buildRequestWithHistory(a, &cfg, "hi", &.{"QUJD"}, "", "", &.{}, edge_map_suffix);
    defer edged.deinit(a);
    try testing.expect(std.mem.indexOf(u8, edged.body, suffixed) != null);
    try testing.expect(std.mem.indexOf(u8, edged.body, sys) == null); // suffixed, not both

    var empty = try buildRequestWithHistory(a, &cfg, "hi", &.{}, "", "", &.{}, "");
    defer empty.deinit(a);
    try testing.expectEqualStrings(plain.body, empty.body);
}

test "extractReply: a body that is not JSON at all is a bad reply, clipped for the detail" {
    const a = testing.allocator;
    const b1 = try extractReply(a, .openai_compat, "not json at all");
    defer b1.deinit(a);
    try testing.expect(b1 == .bad_reply);

    var big: std.ArrayList(u8) = .empty;
    defer big.deinit(a);
    try big.appendSlice(a, "{\"weird\":\"");
    for (0..2 * clip_limit) |_| try big.append(a, 'z');
    try big.appendSlice(a, "\"}");
    const b2 = try extractReply(a, .ollama, big.items);
    defer b2.deinit(a);
    try testing.expect(b2 == .bad_reply);
    try testing.expectEqual(@as(usize, clip_limit + "…".len), b2.bad_reply.len);
    try testing.expect(std.mem.endsWith(u8, b2.bad_reply, "…"));
}

test {
    _ = json;
    _ = chatDoc;
    _ = request;
    _ = body;
    _ = reply;
}

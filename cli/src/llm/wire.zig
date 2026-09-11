//! Wire layer for the console LLM assistant: chat history + the §12.1 persisted
//! chat document, the three provider request mappings (§6), and reply extraction
//! (incl. the stencil-server stopReason contract).
const std = @import("std");
const config = @import("config.zig");
const opplan = @import("opplan.zig");
const registry = @import("registry.zig");
const transport = @import("transport.zig");

// Symbols living in the sibling llm/ modules (facade: ../llm.zig).
const ClipBuf = transport.ClipBuf;
const Config = config.Config;
const Provider = config.Provider;
const clip = transport.clip;
const clip_limit = transport.clip_limit;
const edge_map_suffix = registry.edge_map_suffix;
const systemPrompt = registry.systemPrompt;
const trimUrl = config.trimUrl;

// Chat history & persistence (contract §7 + §12)

/// The §7/§12 history bound: at most this many messages are replayed or persisted.
pub const max_chat_messages = 32;

pub const ChatRole = enum { user, assistant };

/// One text-only conversation turn: replayed before the current prompt (§7) and persisted
/// in the §12.1 chat document. Assistant texts are the DISPLAYED reply, never a raw plan.
pub const Turn = struct { role: ChatRole, text: []const u8 };

/// The most recent ≤ 32 turns of `history` (the §7/§12 bound, applied on write AND read).
fn boundedHistory(history: []const Turn) []const Turn {
    if (history.len > max_chat_messages) return history[history.len - max_chat_messages ..];
    return history;
}

/// §7's auto-continuation note: the internal sentence /prompt appends to the RESTATED request
/// after a plan loaded a picture. It lives here, beside the §12 rules, so the one place that
/// writes it and the one place that must never persist it agree by construction.
pub const continuation_note = "[The working image is now the picture those actions loaded — " ++
    "continue with it, using its real pixel size.]";

const continuation_note_open = "[The working image is now";

/// §12.1: the shared document must read as a conversation, so machinery never enters it —
/// §7's continuation note is stripped (a bracketed variant standing alone drops the turn)
/// and a raw-op-plan ASSISTANT turn drops. Applied on both write and read sides.
pub fn chatDisplayText(role: ChatRole, text: []const u8) ?[]const u8 {
    const ws = " \t\r\n";
    var t = std.mem.trim(u8, text, ws);
    if (std.mem.endsWith(u8, t, "]")) {
        if (std.mem.lastIndexOf(u8, t, continuation_note_open)) |at|
            t = std.mem.trimEnd(u8, t[0..at], ws);
    }
    if (t.len == 0) return null;
    if (role == .assistant and looksLikeRawPlan(t)) return null;
    return t;
}

/// A raw op-plan: a `{…}`/`[…]` carrying "version" plus one of the plan's own fields.
fn looksLikeRawPlan(t: []const u8) bool {
    if (t[0] != '{' and t[0] != '[') return false;
    if (!hasJsonKey(t, "version")) return false;
    return hasJsonKey(t, "actions") or hasJsonKey(t, "reply") or
        hasJsonKey(t, "variants") or hasJsonKey(t, "ask");
}

/// `"key"` followed by optional whitespace and `:`, anywhere in `t`.
fn hasJsonKey(t: []const u8, comptime key: []const u8) bool {
    const quoted = "\"" ++ key ++ "\"";
    var from: usize = 0;
    while (std.mem.indexOfPos(u8, t, from, quoted)) |at| {
        const rest = std.mem.trimStart(u8, t[at + quoted.len ..], " \t\r\n");
        if (rest.len != 0 and rest[0] == ':') return true;
        from = at + quoted.len;
    }
    return false;
}

/// Serialize a history to the §12.1 persisted-chat JSON document
/// (`{"version":1,"savedAt":<ms>,"messages":[{"role","text"},…]}`) — text-only, images are
/// never persisted, trimmed to the most recent 32 turns. Caller owns the result.
pub fn chatDocAlloc(gpa: std.mem.Allocator, history: []const Turn, saved_at: i64) error{OutOfMemory}![]u8 {
    // The §12.1 gate runs BEFORE the bound, so the 32 kept are the most recent persistable
    // turns. Texts are borrowed from `history` — the document is written straight away.
    var kept: std.ArrayList(Turn) = .empty;
    defer kept.deinit(gpa);
    for (history) |t| {
        if (chatDisplayText(t.role, t.text)) |shown|
            try kept.append(gpa, .{ .role = t.role, .text = shown });
    }
    var aw: std.Io.Writer.Allocating = .init(gpa);
    defer aw.deinit();
    var js: std.json.Stringify = .{ .writer = &aw.writer };
    writeChatDoc(&js, kept.items, saved_at) catch return error.OutOfMemory;
    return aw.toOwnedSlice();
}

fn writeChatDoc(js: *std.json.Stringify, history: []const Turn, saved_at: i64) std.json.Stringify.Error!void {
    try js.beginObject();
    try js.objectField("version");
    try js.write(@as(i64, 1));
    try js.objectField("savedAt");
    try js.write(saved_at);
    try js.objectField("messages");
    try js.beginArray();
    for (boundedHistory(history)) |t| {
        try js.beginObject();
        try js.objectField("role");
        try js.write(@tagName(t.role));
        try js.objectField("text");
        try js.write(t.text);
        try js.endObject();
    }
    try js.endArray();
    try js.endObject();
}

/// Parse a §12.1 chat document into turns (free with freeTurns). Tolerant: an off-shape
/// document reads as empty, unknown roles / text-less messages drop, "images" is ignored,
/// only the most recent 32 survive; the §12.1 gate (chatDisplayText) runs here too.
pub fn parseChatDoc(gpa: std.mem.Allocator, bytes: []const u8) error{OutOfMemory}![]Turn {
    var out: std.ArrayList(Turn) = .empty;
    errdefer {
        for (out.items) |t| gpa.free(t.text);
        out.deinit(gpa);
    }
    var parsed = std.json.parseFromSlice(std.json.Value, gpa, bytes, .{}) catch
        return out.toOwnedSlice(gpa);
    defer parsed.deinit();
    const root = parsed.value;
    const ver = member(root, "version") orelse return out.toOwnedSlice(gpa);
    if (ver != .integer or ver.integer != 1) return out.toOwnedSlice(gpa);
    const msgs = member(root, "messages") orelse return out.toOwnedSlice(gpa);
    if (msgs != .array) return out.toOwnedSlice(gpa);
    for (msgs.array.items) |m| {
        const role_s = memberStr(m, "role") orelse continue; // off-shape message → dropped
        const role = std.meta.stringToEnum(ChatRole, role_s) orelse continue; // other roles drop
        const text = memberStr(m, "text") orelse continue; // a message without a text drops
        const shown = chatDisplayText(role, text) orelse continue; // §12.1: never machinery
        const dup = try gpa.dupe(u8, shown);
        out.append(gpa, .{ .role = role, .text = dup }) catch |e| {
            gpa.free(dup);
            return e;
        };
        if (out.items.len > max_chat_messages) {
            gpa.free(out.items[0].text);
            _ = out.orderedRemove(0);
        }
    }
    return out.toOwnedSlice(gpa);
}

/// Free a turn slice returned by parseChatDoc (the texts + the slice itself).
pub fn freeTurns(gpa: std.mem.Allocator, turns: []Turn) void {
    for (turns) |t| gpa.free(t.text);
    gpa.free(turns);
}

// Wire mappings (contract §6)

/// One ready-to-send request: URL, optional `Authorization` value, JSON body. All owned.
pub const Request = struct {
    url: []u8,
    auth: ?[]u8 = null, // full header value ("Bearer <…>")
    body: []u8,

    pub fn deinit(self: *Request, gpa: std.mem.Allocator) void {
        gpa.free(self.url);
        if (self.auth) |a| gpa.free(a);
        gpa.free(self.body);
        self.* = .{ .url = &.{}, .body = &.{} };
    }
};

/// Build the provider request for one user turn (§6). `images` are the attached base64
/// PNGs in order. For `stencil-server` the endpoint + bearer come from
/// `server_url`/`server_token` (caller-resolved); both are ignored elsewhere.
pub fn buildRequest(
    gpa: std.mem.Allocator,
    cfg: *const Config,
    prompt: []const u8,
    images: []const []const u8,
    server_url: []const u8,
    server_token: []const u8,
) error{OutOfMemory}!Request {
    return buildRequestWithHistory(gpa, cfg, prompt, images, server_url, server_token, &.{}, "");
}

/// buildRequest with a replayed conversation and an optional system-prompt suffix:
/// `history` (text-only, most recent 32, §7/§12) rides BEFORE the current turn, the suffix
/// after a blank line (§4). Empty history + suffix = byte-for-byte buildRequest.
pub fn buildRequestWithHistory(
    gpa: std.mem.Allocator,
    cfg: *const Config,
    prompt: []const u8,
    images: []const []const u8,
    server_url: []const u8,
    server_token: []const u8,
    history: []const Turn,
    system_suffix: []const u8,
) error{OutOfMemory}!Request {
    return buildRequestWithSystem(gpa, cfg, systemPrompt(), prompt, images, server_url, server_token, history, system_suffix);
}

/// buildRequestWithHistory over an explicit system-prompt base: the console passes
/// `consoleSystemPrompt()` (the settings block spliced into §4's op list); everything
/// else — wire shape, history replay, suffix joining — is identical.
pub fn buildRequestWithSystem(
    gpa: std.mem.Allocator,
    cfg: *const Config,
    system_base: []const u8,
    prompt: []const u8,
    images: []const []const u8,
    server_url: []const u8,
    server_token: []const u8,
    history: []const Turn,
    system_suffix: []const u8,
) error{OutOfMemory}!Request {
    const joined: ?[]u8 = if (system_suffix.len == 0)
        null
    else
        try std.mem.join(gpa, "\n\n", &.{ system_base, system_suffix });
    defer if (joined) |j| gpa.free(j);
    const system: []const u8 = joined orelse system_base;

    var aw: std.Io.Writer.Allocating = .init(gpa);
    defer aw.deinit();
    var js: std.json.Stringify = .{ .writer = &aw.writer };
    writeBody(&js, cfg, system, prompt, images, boundedHistory(history)) catch return error.OutOfMemory;
    const body = try aw.toOwnedSlice();
    errdefer gpa.free(body);

    const url = switch (cfg.provider) {
        .ollama => try std.fmt.allocPrint(gpa, "{s}/api/chat", .{cfg.base_url}),
        .openai_compat => try std.fmt.allocPrint(gpa, "{s}/chat/completions", .{cfg.base_url}),
        .stencil_server => try std.fmt.allocPrint(gpa, "{s}/llm/chat", .{trimUrl(server_url)}),
    };
    errdefer gpa.free(url);

    const secret: []const u8 = switch (cfg.provider) {
        .ollama => "",
        .openai_compat => cfg.api_key,
        .stencil_server => server_token,
    };
    const auth: ?[]u8 = if (secret.len != 0)
        try std.fmt.allocPrint(gpa, "Bearer {s}", .{secret})
    else
        null;

    return .{ .url = url, .auth = auth, .body = body };
}

fn writeBody(js: *std.json.Stringify, cfg: *const Config, system: []const u8, prompt: []const u8, images: []const []const u8, history: []const Turn) std.json.Stringify.Error!void {
    switch (cfg.provider) {
        // §6.1 — native Ollama chat: images ride as bare base64 strings, in order.
        .ollama => {
            try openChatBody(js, cfg, system, history);
            try js.write(prompt);
            if (images.len != 0) {
                try js.objectField("images");
                try js.beginArray();
                for (images) |b64| try js.write(b64);
                try js.endArray();
            }
            try closeChatBody(js);
        },
        // §6.2 — OpenAI-compatible: one data-URL content part per image, after the text.
        .openai_compat => {
            try openChatBody(js, cfg, system, history);
            if (images.len != 0) {
                try js.beginArray();
                try js.beginObject();
                try js.objectField("type");
                try js.write("text");
                try js.objectField("text");
                try js.write(prompt);
                try js.endObject();
                for (images) |b64| {
                    try js.beginObject();
                    try js.objectField("type");
                    try js.write("image_url");
                    try js.objectField("image_url");
                    try js.beginObject();
                    try js.objectField("url");
                    // Streamed raw to skip an intermediate data-URL copy of the image; base64
                    // never needs JSON escaping, so the bytes match js.write of the same string.
                    try js.beginWriteRaw();
                    try js.writer.writeAll("\"data:image/png;base64,");
                    try js.writer.writeAll(b64);
                    try js.writer.writeAll("\"");
                    js.endWriteRaw();
                    try js.endObject();
                    try js.endObject();
                }
                try js.endArray();
            } else {
                try js.write(prompt);
            }
            try closeChatBody(js);
        },
        // §6.3 — the collaboration server's Anthropic proxy (protocol.LlmChatRequest).
        .stencil_server => {
            try js.beginObject();
            try js.objectField("system");
            try js.write(system);
            try js.objectField("messages");
            try js.beginArray();
            // Replayed history rides before the current turn, text-only (§7/§12).
            for (history) |t| {
                try js.beginObject();
                try js.objectField("role");
                try js.write(@tagName(t.role));
                try js.objectField("text");
                try js.write(t.text);
                try js.endObject();
            }
            try js.beginObject();
            try js.objectField("role");
            try js.write("user");
            try js.objectField("text");
            try js.write(prompt);
            if (images.len != 0) {
                try js.objectField("images");
                try js.beginArray();
                for (images) |b64| {
                    try js.beginObject();
                    try js.objectField("mediaType");
                    try js.write("image/png");
                    try js.objectField("data");
                    try js.write(b64);
                    try js.endObject();
                }
                try js.endArray();
            }
            try js.endObject();
            try js.endArray();
            if (cfg.model.len != 0) {
                try js.objectField("model");
                try js.write(cfg.model);
            }
            try js.endObject();
        },
    }
}

fn writeRoleContent(js: *std.json.Stringify, role: []const u8, content: []const u8) std.json.Stringify.Error!void {
    try js.beginObject();
    try js.objectField("role");
    try js.write(role);
    try js.objectField("content");
    try js.write(content);
    try js.endObject();
}

/// The shared §6.1/§6.2 chat-body prologue: model/stream/messages + system + history +
/// the user message opened up to its `content` value (where the two mappings differ).
/// Balanced by `closeChatBody`.
fn openChatBody(js: *std.json.Stringify, cfg: *const Config, system: []const u8, history: []const Turn) std.json.Stringify.Error!void {
    try js.beginObject();
    try js.objectField("model");
    try js.write(cfg.model);
    try js.objectField("stream");
    try js.write(false);
    try js.objectField("messages");
    try js.beginArray();
    try writeRoleContent(js, "system", system);
    for (history) |t| try writeRoleContent(js, @tagName(t.role), t.text);
    try js.beginObject();
    try js.objectField("role");
    try js.write("user");
    try js.objectField("content");
}

fn closeChatBody(js: *std.json.Stringify) std.json.Stringify.Error!void {
    try js.endObject(); // the user message
    try js.endArray(); // messages
    try js.endObject(); // the body
}

// Reply extraction (contract §6 response shapes + stopReason semantics)

/// What came back from a 2xx provider response. `truncated`/`refusal` are the
/// stencil-server stopReason contract: shown as console errors, NEVER parsed as plans.
pub const Extracted = union(enum) {
    text: []u8, // the raw reply text (owned) — feed to parsePlan
    truncated, // stopReason "max_tokens"
    refusal: []u8, // stopReason "refusal"; the refusal text (owned, may be empty)
    bad_reply: []u8, // not in the documented shape; a clipped detail (owned)

    pub fn deinit(self: Extracted, gpa: std.mem.Allocator) void {
        switch (self) {
            .text, .refusal, .bad_reply => |s| if (s.len != 0) gpa.free(s),
            .truncated => {},
        }
    }
};

pub fn extractReply(gpa: std.mem.Allocator, provider: Provider, body: []const u8) error{OutOfMemory}!Extracted {
    var parsed = std.json.parseFromSlice(std.json.Value, gpa, body, .{}) catch
        return badReply(gpa, body);
    defer parsed.deinit();
    const root = parsed.value;
    if (root != .object) return badReply(gpa, body);

    switch (provider) {
        // Reply text = `message.content`.
        .ollama => {
            if (member(root, "message")) |m| {
                if (memberStr(m, "content")) |c| return .{ .text = try gpa.dupe(u8, c) };
            }
            return badReply(gpa, body);
        },
        // Reply text = `choices[0].message.content`.
        .openai_compat => {
            if (member(root, "choices")) |ch| {
                if (ch == .array and ch.array.items.len != 0) {
                    if (member(ch.array.items[0], "message")) |m| {
                        if (memberStr(m, "content")) |c| return .{ .text = try gpa.dupe(u8, c) };
                    }
                }
            }
            return badReply(gpa, body);
        },
        // protocol.LlmChatResponse: `text` + `stopReason` (max_tokens / refusal are typed).
        .stencil_server => {
            const text = memberStr(root, "text");
            const stop = memberStr(root, "stopReason") orelse "end_turn";
            if (std.mem.eql(u8, stop, "max_tokens")) return .truncated;
            if (std.mem.eql(u8, stop, "refusal")) return .{ .refusal = try gpa.dupe(u8, text orelse "") };
            const t = text orelse return badReply(gpa, body);
            return .{ .text = try gpa.dupe(u8, t) };
        },
    }
}

fn badReply(gpa: std.mem.Allocator, body: []const u8) error{OutOfMemory}!Extracted {
    var buf: ClipBuf = undefined;
    return .{ .bad_reply = try gpa.dupe(u8, clip(&buf, body)) };
}

/// The named member of a JSON object value, or null (also when `v` isn't an object).
pub fn member(v: std.json.Value, key: []const u8) ?std.json.Value {
    if (v != .object) return null;
    return v.object.get(key);
}

/// The named member when it is a string, or null.
pub fn memberStr(v: std.json.Value, key: []const u8) ?[]const u8 {
    const m = member(v, key) orelse return null;
    return if (m == .string) m.string else null;
}

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

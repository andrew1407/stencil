//! Chat history and the §12.1 persisted chat document: the 32-turn bound applied on
//! write AND read, what an assistant turn DISPLAYS (never a raw plan), and the
//! round-trip through the document's JSON shape.
const std = @import("std");
const json = @import("json.zig");

const member = json.member;
const memberStr = json.memberStr;

// Chat history & persistence (contract §7 + §12)

/// The §7/§12 history bound: at most this many messages are replayed or persisted.
pub const max_chat_messages = 32;

pub const ChatRole = enum { user, assistant };

/// One text-only conversation turn: replayed before the current prompt (§7) and persisted
/// in the §12.1 chat document. Assistant texts are the DISPLAYED reply, never a raw plan.
pub const Turn = struct { role: ChatRole, text: []const u8 };

/// The most recent ≤ 32 turns of `history` (the §7/§12 bound, applied on write AND read).
pub fn boundedHistory(history: []const Turn) []const Turn {
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

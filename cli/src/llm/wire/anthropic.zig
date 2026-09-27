//! §6.5 — the direct anthropic wire: the Messages API body (exactly the server upstream's,
//! server/internal/llm/anthropic.go) and what its 2xx reply says. Text blocks lead a turn,
//! its images follow in order; the reply is the concatenation of the `text` blocks.
const std = @import("std");
const config = @import("../config.zig");
const providers = @import("../providers.zig");
const chatDoc = @import("chatDoc.zig");
const json = @import("json.zig");

const Config = config.Config;
const Turn = chatDoc.Turn;
const member = json.member;
const memberStr = json.memberStr;

pub fn writeBody(js: *std.json.Stringify, cfg: *const Config, system: []const u8, prompt: []const u8, images: []const []const u8, history: []const Turn) std.json.Stringify.Error!void {
    const d = providers.get();
    try js.beginObject();
    try js.objectField("model");
    try js.write(if (cfg.model.len != 0) cfg.model else d.default_model);
    try js.objectField("max_tokens");
    try js.write(d.max_tokens);
    try js.objectField("system");
    try js.write(system);
    try js.objectField("messages");
    try js.beginArray();
    // Replayed history rides before the current turn, text-only (§7/§12).
    for (history) |t| try writeTurn(js, @tagName(t.role), t.text, &.{});
    try writeTurn(js, "user", prompt, images);
    try js.endArray();
    try js.endObject();
}

/// One message: its text block (none when the text is empty), then one base64 PNG block per image.
fn writeTurn(js: *std.json.Stringify, role: []const u8, text: []const u8, images: []const []const u8) std.json.Stringify.Error!void {
    try js.beginObject();
    try js.objectField("role");
    try js.write(role);
    try js.objectField("content");
    try js.beginArray();
    if (text.len != 0) {
        try js.beginObject();
        try js.objectField("type");
        try js.write("text");
        try js.objectField("text");
        try js.write(text);
        try js.endObject();
    }
    for (images) |b64| {
        try js.beginObject();
        try js.objectField("type");
        try js.write("image");
        try js.objectField("source");
        try js.beginObject();
        try js.objectField("type");
        try js.write("base64");
        try js.objectField("media_type");
        try js.write("image/png");
        try js.objectField("data");
        try js.write(b64);
        try js.endObject();
        try js.endObject();
    }
    try js.endArray();
    try js.endObject();
}

/// What a 2xx Messages reply means: `stop_reason` max_tokens / refusal are typed outcomes, a
/// body with no `content` array is off-shape (null), anything else is its text blocks joined.
pub const Reply = union(enum) { text: []u8, truncated, refusal: []u8 };

pub fn extract(gpa: std.mem.Allocator, root: std.json.Value) error{OutOfMemory}!?Reply {
    // The stop reason is read first, as the server's stopReason is (reply.zig).
    const stop = memberStr(root, "stop_reason") orelse "end_turn";
    if (std.mem.eql(u8, stop, "max_tokens")) return .truncated;
    const refused = std.mem.eql(u8, stop, "refusal");
    const content = member(root, "content");
    const blocks: []const std.json.Value = if (content != null and content.? == .array) content.?.array.items else if (refused) &.{} else return null;
    var out: std.ArrayList(u8) = .empty;
    errdefer out.deinit(gpa);
    for (blocks) |block| {
        const kind = memberStr(block, "type") orelse continue;
        if (!std.mem.eql(u8, kind, "text")) continue;
        try out.appendSlice(gpa, memberStr(block, "text") orelse "");
    }
    const text = try out.toOwnedSlice(gpa);
    return if (refused) .{ .refusal = text } else .{ .text = text };
}

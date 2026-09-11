//! What came back from a 2xx provider response (§6 response shapes), including the
//! stencil-server stopReason contract: a truncation or refusal is a console error, never
//! a plan.
const std = @import("std");
const config = @import("../config.zig");
const transport = @import("../transport.zig");
const json = @import("json.zig");

const Provider = config.Provider;
const ClipBuf = transport.ClipBuf;
const clip = transport.clip;
const member = json.member;
const memberStr = json.memberStr;

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

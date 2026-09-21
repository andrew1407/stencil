//! What a failed provider response is allowed to say: the sanitized detail (no key, no URL,
//! bounded) and the clip every raw body goes through before it can be printed.
const std = @import("std");
const sanitize = @import("../../safety/sanitize.zig");
const wire = @import("../wire.zig");

const member = wire.member;
const memberStr = wire.memberStr;
const isLlmDisabled = @import("../transport.zig").isLlmDisabled;

/// The ONE sanitizer (sanitize.zig): the caps and the redaction rules live there.
pub const detail_limit = sanitize.detail_limit;
pub const DetailBuf = sanitize.DetailBuf;
pub const sanitizeDetail = sanitize.sanitizeDetail;

/// The provider's own message from a non-2xx body (the three §6 error shapes), sanitized by
/// `sanitizeDetail`; empty when the body carries nothing usable. The raw body is NEVER printed.
pub fn errorDetail(gpa: std.mem.Allocator, body: []const u8, out: *DetailBuf) []const u8 {
    var parsed = std.json.parseFromSlice(std.json.Value, gpa, body, .{}) catch return "";
    defer parsed.deinit();
    const root = parsed.value;
    const raw = memberStr(root, "message") orelse blk: {
        const e = member(root, "error") orelse return "";
        break :blk switch (e) {
            .string => |s| s,
            .object => memberStr(e, "message") orelse return "",
            else => return "",
        };
    };
    return sanitizeDetail(raw, out);
}

/// How much of a response body is quoted in error messages.
pub const clip_limit = 300;
pub const ClipBuf = [clip_limit + "…".len]u8;

/// Bound a response body for inclusion in a message (≤ `clip_limit` bytes, backed off to a
/// UTF-8 codepoint boundary, with an ellipsis). Returns a slice of `buf` or of `body` itself.
pub fn clip(buf: *ClipBuf, body: []const u8) []const u8 {
    const t = std.mem.trim(u8, body, " \t\r\n");
    if (t.len <= clip_limit) return t;
    var end: usize = clip_limit;
    while (end > 0 and (t[end] & 0xC0) == 0x80) : (end -= 1) {}
    @memcpy(buf[0..end], t[0..end]);
    const ell = "…";
    @memcpy(buf[end .. end + ell.len], ell);
    return buf[0 .. end + ell.len];
}

const testing = std.testing;

test "errorDetail: the provider's reason, said once and never the raw body" {
    const a = testing.allocator;
    var buf: DetailBuf = undefined;
    // stencil-server {code,message}: the reason alone — no status, no upstream prose.
    try testing.expectEqualStrings(
        "the LLM provider is out of credits or has no active billing",
        errorDetail(a, "{\"code\":\"llmUpstream\",\"message\":\"the LLM provider is out of credits or has no active billing\"}", &buf),
    );
    // ollama {"error":"…"} and openai-compat {"error":{"message":"…"}}.
    try testing.expectEqualStrings("model 'x' not found", errorDetail(a, "{\"error\":\"model 'x' not found\"}", &buf));
    try testing.expectEqualStrings("invalid model", errorDetail(a, "{\"error\":{\"message\":\"invalid model\"}}", &buf));
    // Nothing usable (or not JSON at all) quotes nothing — the caller prints the status.
    try testing.expectEqualStrings("", errorDetail(a, "<html>gateway down</html>", &buf));
    try testing.expectEqualStrings("", errorDetail(a, "{\"detail\":\"x\"}", &buf));
}

test "isLlmDisabled: only the server's llmDisabled code types as disabled" {
    const a = testing.allocator;
    try testing.expect(isLlmDisabled(a, "{\"code\":\"llmDisabled\",\"message\":\"LLM is not configured on this server\"}"));
    try testing.expect(!isLlmDisabled(a, "{\"code\":\"llmUpstream\",\"message\":\"out of credits\"}"));
    try testing.expect(!isLlmDisabled(a, "{\"error\":\"model not found\"}"));
    try testing.expect(!isLlmDisabled(a, "<html>gateway down</html>"));
}

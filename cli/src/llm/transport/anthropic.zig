//! §6.5: why a direct anthropic call failed, said once. The Anthropic error envelope and the
//! status classify into one reason exactly as server/internal/llm/upstream.go does; only an
//! unrecognised failure quotes the upstream's sanitized text, and never any run of the key.
const std = @import("std");
const sanitize = @import("../../safety/sanitize.zig");
const json = @import("../wire/json.zig");

const member = json.member;
const memberStr = json.memberStr;

pub const Kind = enum { credits, auth, model, rate_limit, timeout, overloaded, unknown };

/// The provider's own error type and the status first, string matching after (upstream.go).
pub fn classify(status: u16, err_type: []const u8, text: []const u8) Kind {
    if (hasAny(err_type, &.{ "insufficient_quota", "billing", "credit" }) or
        hasAny(text, &.{ "credit balance", "insufficient_quota", "insufficient quota", "purchase credits", "billing", "out of credits" }) or
        status == 402) return .credits;
    if (hasAny(err_type, &.{ "authentication", "invalid_api_key", "permission", "unauthorized", "forbidden" }) or
        status == 401 or status == 403) return .auth;
    if (hasAny(err_type, &.{ "model_not_found", "not_found" }) or
        hasAny(text, &.{ "model not found", "unknown model", "does not exist", "try pulling", "no such model" }) or
        status == 404) return .model;
    if (hasAny(err_type, &.{"rate_limit"}) or status == 429) return .rate_limit;
    if (status == 408 or status == 504) return .timeout;
    if (hasAny(err_type, &.{ "overloaded", "api_error" }) or status >= 500) return .overloaded;
    return .unknown;
}

/// The contract's wording per reason (§6.5 table); `unknown` is the prefix the status follows.
pub fn reason(kind: Kind) []const u8 {
    return switch (kind) {
        .credits => "the LLM provider is out of credits or has no active billing",
        .auth => "the LLM provider rejected the API key",
        .model => "the LLM provider does not have the requested model",
        .rate_limit => "the LLM provider is rate-limiting this key",
        .timeout => "the LLM provider did not respond in time",
        .overloaded => "the LLM provider is temporarily unavailable",
        .unknown => "the LLM provider returned an error",
    };
}

pub const MessageBuf = [96 + sanitize.detail_limit + "…".len]u8;

/// The line a non-2xx answer prints: `{type:"error",error:{type,message}}` classified, the
/// upstream's own text kept only when nothing is recognised and no 8-byte run of `key` shows.
pub fn message(gpa: std.mem.Allocator, status: u16, body: []const u8, key: []const u8, out: *MessageBuf) []const u8 {
    var parsed = std.json.parseFromSlice(std.json.Value, gpa, body, .{}) catch null;
    defer if (parsed) |*p| p.deinit();
    const env: ?std.json.Value = if (parsed) |p| member(p.value, "error") else null;
    const text = if (env) |e| memberStr(e, "message") orelse "" else "";
    // As upstream.go: without an envelope message, the status alone decides.
    const err_type = if (text.len != 0) memberStr(env.?, "type") orelse "" else "";
    const kind = classify(status, err_type, text);
    if (kind != .unknown) return reason(kind);
    var dbuf: sanitize.DetailBuf = undefined;
    const said = sanitize.sanitizeDetail(text, &dbuf);
    const keep = said.len != 0 and !showsKeyRun(said, key);
    return std.fmt.bufPrint(out, "{s} (HTTP {d}){s}{s}", .{ reason(.unknown), status, if (keep) ": " else "", if (keep) said else "" }) catch reason(.unknown);
}

/// True when `text` shows any 8-byte run of `key` — a partial key is still a leak.
pub fn showsKeyRun(text: []const u8, key: []const u8) bool {
    const run = 8;
    if (key.len < run) return false;
    for (0..key.len - run + 1) |i| {
        if (std.mem.indexOf(u8, text, key[i..][0..run]) != null) return true;
    }
    return false;
}

fn hasAny(s: []const u8, needles: []const []const u8) bool {
    for (needles) |n| if (std.ascii.indexOfIgnoreCase(s, n) != null) return true;
    return false;
}

const testing = std.testing;

test "classify: the envelope type and the status pick one reason (upstream.go order)" {
    try testing.expectEqual(Kind.credits, classify(429, "rate_limit_error", "Your credit balance is too low"));
    try testing.expectEqual(Kind.auth, classify(400, "authentication_error", ""));
    try testing.expectEqual(Kind.model, classify(400, "invalid_request_error", "Unknown model: x"));
    try testing.expectEqual(Kind.timeout, classify(504, "", ""));
    try testing.expectEqual(Kind.overloaded, classify(500, "", ""));
    try testing.expectEqual(Kind.unknown, classify(418, "", "teapot"));
}

test "message: a body that is not the envelope falls back to its status" {
    var buf: MessageBuf = undefined;
    const a = testing.allocator;
    try testing.expectEqualStrings("the LLM provider is temporarily unavailable", message(a, 502, "<html>bad gateway</html>", "", &buf));
    try testing.expectEqualStrings("the LLM provider returned an error (HTTP 418)", message(a, 418, "{}", "", &buf));
    // A short key has no 8-byte run to veto; the text still passes the sanitizer.
    try testing.expectEqualStrings(
        "the LLM provider returned an error (HTTP 400): bad [redacted]",
        message(a, 400, "{\"error\":{\"type\":\"x\",\"message\":\"bad https://10.0.0.1/v1\"}}", "k", &buf),
    );
}

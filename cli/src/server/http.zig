//! The server transport seam and the last-rejection record. Zig errors carry no payload,
//! so a non-2xx response's status and (sanitized) message are parked here for the caller.
const std = @import("std");
const net = @import("../net.zig");
const sanitize = @import("../sanitize.zig");
const errors = @import("errors.zig");
const parse = @import("parse.zig");

const Error = errors.Error;
const TransportError = errors.TransportError;
const parseErrorMessage = parse.parseErrorMessage;

/// HTTP seam: rawRequest in production, swappable in tests.
pub const Transport = *const fn (
    gpa: std.mem.Allocator,
    io: std.Io,
    url: []const u8,
    method: std.http.Method,
    payload: ?[]const u8,
    headers: []const std.http.Header,
) TransportError![]u8;

// Zig errors carry no payload, so the last non-2xx status + server message are kept here for
// connect()'s callers. Thread-local, so a fan-out never overwrites the caller's rejection.
threadlocal var reject_status: u32 = 0;
threadlocal var reject_buf: [256]u8 = undefined;
threadlocal var reject_len: usize = 0;

pub const Reject = struct { status: u32, message: []const u8 };

/// The last non-2xx response's status + message, or null when the last request
/// succeeded or never reached the server.
pub fn lastReject() ?Reject {
    if (reject_status == 0) return null;
    return .{ .status = reject_status, .message = reject_buf[0..reject_len] };
}

pub const RejectCopy = struct { status: u32, buf: [256]u8, len: usize };

pub fn saveReject() RejectCopy {
    return .{ .status = reject_status, .buf = reject_buf, .len = reject_len };
}

pub fn restoreReject(r: RejectCopy) void {
    reject_status = r.status;
    reject_buf = r.buf;
    reject_len = r.len;
}

pub fn recordReject(gpa: std.mem.Allocator, status: u32, body: []const u8) void {
    reject_status = status;
    const msg = parseErrorMessage(gpa, body);
    defer if (msg) |m| gpa.free(m);
    // The server's prose is untrusted: bound it and strip keys/URLs before it can be printed.
    var buf: sanitize.DetailBuf = undefined;
    const src = sanitize.sanitizeDetail(msg orelse body, &buf);
    reject_len = @min(src.len, reject_buf.len);
    @memcpy(reject_buf[0..reject_len], src[0..reject_len]);
}

/// One-shot HTTP request with explicit headers; returns owned response body bytes. Runs over
/// net.request: response capped, redirect refused, host block skipped (the user's own server).
pub fn rawRequest(
    gpa: std.mem.Allocator,
    io: std.Io,
    url: []const u8,
    method: std.http.Method,
    payload: ?[]const u8,
    headers: []const std.http.Header,
) TransportError![]u8 {
    reject_status = 0; // a transport failure below leaves no stale rejection behind
    const opts: net.RequestOptions = .{ .method = method, .payload = payload, .extra_headers = headers, .allow_named_host = true };
    const res = net.request(gpa, io, url, opts) catch |e| return if (e == error.OutOfMemory) error.OutOfMemory else Error.HttpFailed;

    const code = res.status;
    if (code < 200 or code >= 300) {
        defer gpa.free(res.body);
        recordReject(gpa, code, res.body);
        if (code == 401) return Error.Unauthorized;
        if (code == 404) return Error.NotFound;
        if (code == 409) return Error.Conflict;
        return Error.HttpFailed;
    }
    return res.body;
}

const testing = std.testing;

test "recordReject keeps status + message; falls back to the raw body; save/restore round-trips" {
    const a = testing.allocator;
    recordReject(a, 401, "{\"code\":\"unauthorized\",\"message\":\"missing or invalid token\"}");
    var r = lastReject().?;
    try testing.expectEqual(@as(u32, 401), r.status);
    try testing.expectEqualStrings("missing or invalid token", r.message);

    // A non-JSON body is reported raw (trimmed).
    const saved = saveReject();
    recordReject(a, 503, "  service melting\n");
    r = lastReject().?;
    try testing.expectEqual(@as(u32, 503), r.status);
    try testing.expectEqualStrings("service melting", r.message);

    // restoreReject brings the earlier rejection back (used by the admin-mint retry).
    restoreReject(saved);
    r = lastReject().?;
    try testing.expectEqual(@as(u32, 401), r.status);
    try testing.expectEqualStrings("missing or invalid token", r.message);

    // The server's prose is sanitized on the way in: no key, no URL, and bounded.
    recordReject(a, 500, "{\"code\":\"x\",\"message\":\"upstream http://10.0.0.5:9000/llm rejected sk-abcdef1234567890\"}");
    r = lastReject().?;
    try testing.expectEqualStrings("upstream [redacted] rejected [redacted]", r.message);
    var long: [600]u8 = undefined;
    for (&long, 0..) |*c, i| c.* = if (i % 5 == 4) ' ' else 'a';
    recordReject(a, 500, &long);
    try testing.expect(lastReject().?.message.len <= sanitize.detail_limit + "…".len);

    reject_status = 0; // leave no cross-test state
}

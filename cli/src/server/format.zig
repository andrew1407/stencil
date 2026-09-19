//! Human time spans for the `/projects` table: how long ago a project changed, and how
//! long until it expires. Both write into a caller buffer — no allocation, no locale.
const std = @import("std");
const testing = std.testing;

/// Render an epoch-ms timestamp as a short human "… ago" string relative to `now_ms`, into `buf`. A
/// zero/missing or future timestamp reads as "just now". Pure — unit-tested.
pub fn formatAgo(buf: []u8, now_ms: i64, then_ms: i64) []const u8 {
    if (then_ms <= 0) return "just now";
    const delta = if (now_ms > then_ms) now_ms - then_ms else 0;
    const secs = @divTrunc(delta, 1000);
    if (secs < 5) return "just now";
    if (secs < 60) return std.fmt.bufPrint(buf, "{d}s ago", .{secs}) catch "moments ago";
    const mins = @divTrunc(secs, 60);
    if (mins < 60) return std.fmt.bufPrint(buf, "{d}m ago", .{mins}) catch "a while ago";
    const hours = @divTrunc(mins, 60);
    if (hours < 24) return std.fmt.bufPrint(buf, "{d}h ago", .{hours}) catch "a while ago";
    return std.fmt.bufPrint(buf, "{d}d ago", .{@divTrunc(hours, 24)}) catch "a while ago";
}

/// Render an expiry timestamp as a short "in …" / "expired" / "never" string relative to `now_ms`: zero
/// is "never" (keep forever), at-or-past is "expired". The forward-looking twin of formatAgo.
pub fn formatUntil(buf: []u8, now_ms: i64, then_ms: i64) []const u8 {
    if (then_ms <= 0) return "never";
    if (then_ms <= now_ms) return "expired";
    const delta = then_ms - now_ms;
    const secs = @divTrunc(delta, 1000);
    if (secs < 60) return std.fmt.bufPrint(buf, "in {d}s", .{secs}) catch "soon";
    const mins = @divTrunc(secs, 60);
    if (mins < 60) return std.fmt.bufPrint(buf, "in {d}m", .{mins}) catch "soon";
    const hours = @divTrunc(mins, 60);
    if (hours < 24) return std.fmt.bufPrint(buf, "in {d}h", .{hours}) catch "soon";
    return std.fmt.bufPrint(buf, "in {d}d", .{@divTrunc(hours, 24)}) catch "later";
}

test "formatAgo renders short relative times, just-now for fresh/unknown/future" {
    var buf: [32]u8 = undefined;
    const now: i64 = 1_000_000_000_000;
    try testing.expectEqualStrings("just now", formatAgo(&buf, now, 0)); // missing timestamp
    try testing.expectEqualStrings("just now", formatAgo(&buf, now, now + 5000)); // future clamps
    try testing.expectEqualStrings("just now", formatAgo(&buf, now, now - 2000)); // < 5s
    try testing.expectEqualStrings("30s ago", formatAgo(&buf, now, now - 30_000));
    try testing.expectEqualStrings("5m ago", formatAgo(&buf, now, now - 5 * 60_000));
    try testing.expectEqualStrings("3h ago", formatAgo(&buf, now, now - 3 * 60 * 60_000));
    try testing.expectEqualStrings("2d ago", formatAgo(&buf, now, now - 2 * 24 * 60 * 60_000));
}

test "formatUntil renders forward expiry, never/expired at the edges" {
    var buf: [32]u8 = undefined;
    const now: i64 = 1_000_000_000_000;
    try testing.expectEqualStrings("never", formatUntil(&buf, now, 0)); // keep forever
    try testing.expectEqualStrings("expired", formatUntil(&buf, now, now)); // at boundary
    try testing.expectEqualStrings("expired", formatUntil(&buf, now, now - 1000)); // past
    try testing.expectEqualStrings("in 30s", formatUntil(&buf, now, now + 30_000));
    try testing.expectEqualStrings("in 5m", formatUntil(&buf, now, now + 5 * 60_000));
    try testing.expectEqualStrings("in 3h", formatUntil(&buf, now, now + 3 * 60 * 60_000));
    try testing.expectEqualStrings("in 2d", formatUntil(&buf, now, now + 2 * 24 * 60 * 60_000));
}

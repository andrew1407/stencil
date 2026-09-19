//! Which slice of the filtered list a run downloads (--source-group / --source-count),
//! and the small option normalizations around it.
const std = @import("std");
const net = @import("../net.zig");
const testing = std.testing;
const sniff = @import("sniff.zig");
const filter = @import("filter.zig");
const Media = filter.Media;
const formatOf = filter.formatOf;
const Sniff = sniff.Sniff;

/// Normalize a CLI `0 = unset` bound to the optional the filter uses.
pub fn boundOpt(v: u32) ?u32 {
    return if (v == 0) null else v;
}

/// Whether a media sub-resource fetch must run strict (loopback blocked): loopback is tolerated ONLY on
/// the SAME host the user named, so a page smuggling another internal host is refused. Unparseable = strict.
pub fn subStrict(media_url: []const u8, page_host: []const u8) bool {
    const mh = net.hostOf(media_url) orelse return true;
    return !std.ascii.eqlIgnoreCase(mh, page_host);
}

/// The download extension: prefer the URL's format token, fall back to the content sniff.
pub fn formatFor(buf: []u8, m: Media, dims: ?Sniff) []const u8 {
    const f = formatOf(buf, m.url);
    if (f.len != 0) return f;
    if (dims) |d| return d.fmt;
    return "";
}

/// Map the user-facing `--source-count` to the low-level `window` count: absent picks the default 5, an
/// explicit `0` means "all" (null), any N passes through. `window` itself keeps null = all.
pub fn effectiveCount(opt: ?u32) ?u32 {
    const n = opt orelse return 5;
    return if (n == 0) null else n;
}

/// The group/count window over `items`: all of it when `count` is null, else
/// `items[group*count .. group*count+count]` (clamped).
pub fn window(comptime T: type, items: []T, group: u32, count: ?u32) []T {
    const n = count orelse return items;
    if (n == 0) return items[0..0];
    const start = @min(@as(usize, group) * n, items.len);
    const end = @min(start + n, items.len);
    return items[start..end];
}

test "subStrict: loopback tolerated only for same-host sub-resources" {
    // Same host as the page → non-strict (a localhost gallery's own images stay fetchable).
    try testing.expect(!subStrict("http://127.0.0.1:8080/a.png", "127.0.0.1"));
    try testing.expect(!subStrict("http://localhost/img.png", "localhost"));
    try testing.expect(!subStrict("http://cdn.example.com/x.png", "cdn.example.com"));
    // Different host → strict (the SSRF pivot: a public page pointing at loopback/internal).
    try testing.expect(subStrict("http://127.0.0.1/admin", "evil.example"));
    try testing.expect(subStrict("http://169.254.169.254/meta", "site.example"));
    try testing.expect(subStrict("http://other.example/x.png", "site.example"));
    // Unparseable media host errs safe (strict).
    try testing.expect(subStrict("http:///only-path", "site.example"));
}

test "effectiveCount: default 5, 0 = all, N passthrough" {
    try testing.expectEqual(@as(?u32, 5), effectiveCount(null)); // absent → default 5
    try testing.expectEqual(@as(?u32, null), effectiveCount(0)); // 0 → all
    try testing.expectEqual(@as(?u32, 3), effectiveCount(3)); // N → N
}

test "window: all vs group/count slicing" {
    var xs = [_]u32{ 0, 1, 2, 3, 4 };
    try testing.expectEqual(@as(usize, 5), window(u32, &xs, 0, null).len); // count absent → all
    try testing.expectEqualSlices(u32, &.{ 0, 1 }, window(u32, &xs, 0, 2));
    try testing.expectEqualSlices(u32, &.{ 2, 3 }, window(u32, &xs, 1, 2));
    try testing.expectEqualSlices(u32, &.{4}, window(u32, &xs, 2, 2)); // clamped tail
    try testing.expectEqual(@as(usize, 0), window(u32, &xs, 5, 2).len); // past the end
}

//! What an error line shows of a URL: `scheme://host/path`, never its userinfo, query or fragment, so
//! a pre-signed CDN query string or an invite `#token=` cannot ride a message into an adapter's
//! tool result, a chat reply or a log.
const std = @import("std");

/// Room for one shown URL; a longer one is cut with "…".
pub const Buf = [512]u8;

pub fn of(buf: *Buf, url: []const u8) []const u8 {
    const end = std.mem.indexOfAny(u8, url, "?#") orelse url.len;
    const bare = url[0..end];
    const authority = if (std.mem.indexOf(u8, bare, "://")) |i| i + 3 else 0;
    const path = std.mem.indexOfScalarPos(u8, bare, authority, '/') orelse bare.len;
    const at = std.mem.lastIndexOfScalar(u8, bare[authority..path], '@');
    const host_from = if (at) |i| authority + i + 1 else authority;
    var w: std.Io.Writer = .fixed(buf);
    w.writeAll(bare[0..authority]) catch {};
    w.writeAll(bare[host_from..]) catch {};
    if (end < url.len) w.writeAll(if (url[end] == '?') "?…" else "#…") catch {};
    const out = w.buffered();
    if (out.len == buf.len) {
        const tail = "…";
        @memcpy(buf[buf.len - tail.len ..], tail);
    }
    return out;
}

const testing = std.testing;

test "a shown URL keeps scheme, host and path, and drops userinfo, query and fragment" {
    var b: Buf = undefined;
    try testing.expectEqualStrings("https://cdn.example/a.png?…", of(&b, "https://cdn.example/a.png?X-Amz-Signature=secret"));
    try testing.expectEqualStrings("https://h.example/p#…", of(&b, "https://h.example/p#token=abc"));
    try testing.expectEqualStrings("http://h.example:8090/x", of(&b, "http://user:pass@h.example:8090/x"));
    try testing.expectEqualStrings("https://plain.example/a.png", of(&b, "https://plain.example/a.png"));
    try testing.expectEqualStrings("photo.png", of(&b, "photo.png"));
}

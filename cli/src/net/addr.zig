//! Reading a host the way a resolver would, before the SSRF table judges it: brackets and a zone
//! ID dropped, IPv4 in every `inet_aton` spelling (hex, octal, decimal, one to four parts), IPv6
//! with an IPv4 tail in any position. Anything else is a name (fixtures/net/hosts.json pins it).
const std = @import("std");

pub const Addr = struct {
    v6: bool,
    bytes: [16]u8 = [_]u8{0} ** 16,

    pub fn v4(b: [4]u8) Addr {
        var a = Addr{ .v6 = false };
        a.bytes[0..4].* = b;
        return a;
    }

    pub fn slice(self: *const Addr) []const u8 {
        return if (self.v6) self.bytes[0..] else self.bytes[0..4];
    }

    pub fn fromIp(ip: std.Io.net.IpAddress) Addr {
        return switch (ip) {
            .ip4 => |a| v4(a.bytes),
            .ip6 => |a| .{ .v6 = true, .bytes = a.bytes },
        };
    }
};

/// One `inet_aton` component: `0x` hex, a leading-`0` octal, else decimal. Digits only — Zig's
/// parseInt would also take a sign or an underscore, which no resolver does.
fn atonPart(s: []const u8) ?u64 {
    if (s.len == 0) return null;
    const hex = s.len >= 2 and s[0] == '0' and (s[1] == 'x' or s[1] == 'X');
    const digits = if (hex) s[2..] else s;
    const base: u8 = if (hex) 16 else if (s.len >= 2 and s[0] == '0') 8 else 10;
    if (digits.len == 0) return 0;
    if (!allDigits(digits, base)) return null;
    return std.fmt.parseInt(u64, digits, base) catch null;
}

fn allDigits(s: []const u8, base: u8) bool {
    for (s) |c| _ = std.fmt.charToDigit(c, base) catch return false;
    return s.len != 0;
}

/// `inet_aton`: 1–4 parts, the last filling the remaining low bytes.
pub fn parseInetAton(host: []const u8) ?[4]u8 {
    if (host.len == 0 or !std.ascii.isDigit(host[0])) return null;
    var parts: [4]u64 = undefined;
    var n: usize = 0;
    var it = std.mem.splitScalar(u8, host, '.');
    while (it.next()) |part| {
        if (n >= 4) return null;
        parts[n] = atonPart(part) orelse return null;
        n += 1;
    }
    const limits = [_]u64{ 0xffff_ffff, 0xff_ffff, 0xffff, 0xff };
    for (parts[0 .. n - 1]) |p| if (p > 0xff) return null;
    if (parts[n - 1] > limits[n - 1]) return null;
    var value: u64 = parts[n - 1];
    for (parts[0 .. n - 1], 0..) |p, i| value |= p << @intCast(8 * (3 - i));
    return .{ @intCast(value >> 24), @intCast((value >> 16) & 0xff), @intCast((value >> 8) & 0xff), @intCast(value & 0xff) };
}

/// Strict dotted-quad decimal, the only IPv4 spelling an IPv6 tail and the table allow.
fn parseDotted(s: []const u8) ?[4]u8 {
    var out: [4]u8 = undefined;
    var n: usize = 0;
    var it = std.mem.splitScalar(u8, s, '.');
    while (it.next()) |part| {
        if (n >= 4 or part.len > 3 or !allDigits(part, 10)) return null;
        out[n] = std.fmt.parseInt(u8, part, 10) catch return null;
        n += 1;
    }
    return if (n == 4) out else null;
}

fn parseGroups(s: []const u8, out: []u16) ?usize {
    if (s.len == 0) return 0;
    var n: usize = 0;
    var it = std.mem.splitScalar(u8, s, ':');
    while (it.next()) |g| {
        if (n >= out.len or g.len > 4 or !allDigits(g, 16)) return null;
        out[n] = std.fmt.parseInt(u16, g, 16) catch return null;
        n += 1;
    }
    return n;
}

/// RFC 4291 text: eight hex groups, one `::` for a run of zeros, an IPv4 tail allowed.
pub fn parseV6(text: []const u8) ?[16]u8 {
    var s = text;
    var tail: ?[4]u8 = null;
    if (std.mem.lastIndexOfScalar(u8, s, ':')) |colon| {
        if (std.mem.indexOfScalarPos(u8, s, colon, '.') != null) {
            tail = parseDotted(s[colon + 1 ..]) orelse return null;
            s = s[0 .. colon + 1];
            if (s.len >= 2 and s[s.len - 2] != ':') s = s[0 .. s.len - 1];
        }
    } else return null;
    const room: usize = if (tail != null) 6 else 8;
    var head: [8]u16 = undefined;
    var rest: [8]u16 = undefined;
    var nh: usize = 0;
    var nr: usize = 0;
    if (std.mem.indexOf(u8, s, "::")) |gap| {
        if (std.mem.indexOfPos(u8, s, gap + 1, "::") != null) return null;
        nh = parseGroups(s[0..gap], &head) orelse return null;
        nr = parseGroups(s[gap + 2 ..], &rest) orelse return null;
        if (nh + nr >= room) return null;
    } else {
        nh = parseGroups(s, &head) orelse return null;
        if (nh != room) return null;
    }
    var groups = [_]u16{0} ** 8;
    for (head[0..nh], 0..) |g, i| groups[i] = g;
    for (rest[0..nr], 0..) |g, i| groups[room - nr + i] = g;
    var out: [16]u8 = undefined;
    for (groups, 0..) |g, i| {
        out[2 * i] = @intCast(g >> 8);
        out[2 * i + 1] = @intCast(g & 0xff);
    }
    if (tail) |t| out[12..16].* = t;
    return out;
}

/// A table entry's address: dotted-quad IPv4, or IPv6.
pub fn parseLiteral(text: []const u8) ?Addr {
    if (std.mem.indexOfScalar(u8, text, ':') != null) return .{ .v6 = true, .bytes = parseV6(text) orelse return null };
    return Addr.v4(parseDotted(text) orelse return null);
}

/// A URL host as a resolver reads it: brackets and a zone ID (`%en0`, `%25en0`) dropped, IPv4 by
/// `inet_aton`. Null means a name.
pub fn parseHost(raw: []const u8) ?Addr {
    var host = raw;
    if (host.len >= 2 and host[0] == '[' and host[host.len - 1] == ']') host = host[1 .. host.len - 1];
    if (std.mem.indexOfScalar(u8, host, ':') != null) {
        if (std.mem.indexOfScalar(u8, host, '%')) |zone| host = host[0..zone];
        return .{ .v6 = true, .bytes = parseV6(host) orelse return null };
    }
    return Addr.v4(parseInetAton(host) orelse return null);
}

const testing = std.testing;

test "addr: the inet_aton spellings and the IPv6 forms a resolver accepts" {
    try testing.expectEqual([4]u8{ 8, 0, 0, 1 }, parseInetAton("010.0.0.1").?);
    try testing.expectEqual([4]u8{ 127, 0, 0, 1 }, parseInetAton("0x7f.1").?);
    try testing.expectEqual([4]u8{ 169, 254, 169, 254 }, parseInetAton("169.254.43518").?);
    try testing.expectEqual([4]u8{ 0, 0, 0, 0 }, parseInetAton("0").?);
    try testing.expect(parseInetAton("999999999999") == null and parseInetAton("1.2.3.4.5") == null);
    try testing.expect(parseHost("0x7f.example") == null and parseHost("1.2.3.4.example.com") == null);
    try testing.expectEqualSlices(u8, parseLiteral("::ffff:127.0.0.1").?.slice(), parseHost("::ffff:7f00:1").?.slice());
    try testing.expectEqualSlices(u8, parseLiteral("64:ff9b::a9fe:a9fe").?.slice(), parseHost("64:ff9b::169.254.169.254").?.slice());
    try testing.expectEqualSlices(u8, parseLiteral("fe80::1").?.slice(), parseHost("[fe80::1%25en0]").?.slice());
    try testing.expectEqualSlices(u8, parseLiteral("::1").?.slice(), parseHost("0:0:0:0:0:0:0:1").?.slice());
    for ([_][]const u8{ ":::1", "1::2::3", "1:2:3:4:5:6:7:8:9", "12345::", "::g", "1:2:3:4:5:6:7" }) |bad|
        try testing.expect(parseV6(bad) == null);
}

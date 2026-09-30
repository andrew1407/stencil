// Walks the shared SSRF host corpus (common/fixtures/net/hosts.json) against the cli's
// guard: every host read as the address it names (bare, and as a URL authority would carry it),
// and judged under all four policy variants of common/config/net/blockedRanges.json.
const std = @import("std");
const addr = @import("../../src/net/addr.zig");
const ranges = @import("../../src/net/ranges.zig");
const host = @import("../../src/net/host.zig");
const fx = @import("../fixture_corpus.zig");
const testing = std.testing;

const Variant = struct { key: []const u8, policy: ranges.Policy, opts: ranges.Options };
const variants = [_]Variant{
    .{ .key = "fetch", .policy = .fetch, .opts = .{} },
    .{ .key = "fetch+allowLoopback", .policy = .fetch, .opts = .{ .allow_loopback = true } },
    .{ .key = "serverTarget", .policy = .server_target, .opts = .{} },
    .{ .key = "serverTarget+allowPrivate", .policy = .server_target, .opts = .{ .allow_private = true } },
};

/// The host as a URL carries it: an unbracketed IPv6 in brackets, `%` written `%25`.
fn viaUrl(a: std.mem.Allocator, h: []const u8) ![]const u8 {
    var inner: std.ArrayList(u8) = .empty;
    for (h) |c| if (c == '%') try inner.appendSlice(a, "%25") else try inner.append(a, c);
    const bare = std.mem.indexOfScalar(u8, h, ':') != null and h[0] != '[';
    const url = try std.fmt.allocPrint(a, "http://{s}{s}{s}/x", .{ if (bare) "[" else "", inner.items, if (bare) "]" else "" });
    return host.hostOf(url).?;
}

fn sameAddress(got: ?addr.Addr, want: ?addr.Addr) bool {
    if (got == null or want == null) return got == null and want == null;
    return got.?.v6 == want.?.v6 and std.mem.eql(u8, got.?.slice(), want.?.slice());
}

test "SSRF host corpus: every host reads as its address and gets every policy's verdict" {
    var w = fx.Walk.start();
    defer w.stop();
    const a = w.alloc();
    const cases = try w.cases("net/hosts.json");
    try testing.expect(cases.len >= 80);
    for (cases) |c| {
        const name = fx.memberStr(c, "name").?;
        const h = fx.memberStr(c, "host").?;
        const want: ?addr.Addr = if (fx.memberStr(c, "address")) |text| addr.parseLiteral(text).? else null;
        w.walked += 1;
        if (!sameAddress(addr.parseHost(h), want)) w.fail("hosts {s}: '{s}' read as the wrong address\n", .{ name, h });
        if (!sameAddress(addr.parseHost(try viaUrl(a, h)), want)) w.fail("hosts {s}: '{s}' through a URL read wrong\n", .{ name, h });
        const expect = fx.member(c, "expect") orelse continue;
        for (variants) |v| {
            const verdict = if (ranges.blocked(want.?, v.policy, v.opts)) "block" else "allow";
            const pinned = fx.memberStr(expect, v.key).?;
            if (!std.mem.eql(u8, verdict, pinned)) w.fail("hosts {s} [{s}]: want {s}, cli says {s}\n", .{ name, v.key, pinned, verdict });
        }
        // The guard's own entry point agrees: `fetch`, loopback allowed unless strict.
        const fetch_open = std.mem.eql(u8, fx.memberStr(expect, "fetch+allowLoopback").?, "block");
        if (host.isBlockedFetchHost(h, false) != fetch_open) w.fail("hosts {s}: isBlockedFetchHost disagrees\n", .{name});
    }
    try w.report("hosts");
}

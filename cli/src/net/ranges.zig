//! The SSRF address table (common/config/net/blockedRanges.json, embedded) and its evaluator:
//! classes of CIDRs, the IPv6 prefixes that carry an IPv4 address, and the two policies —
//! `fetch` and `serverTarget` — each with the option that relaxes it. Semantics: its README.
const std = @import("std");
const addr_mod = @import("addr.zig");

pub const Addr = addr_mod.Addr;

pub const table_json = @embedFile("blockedRanges.json");

pub const Policy = enum { fetch, server_target };

/// `allow_loopback` relaxes `fetch` (a URL the user typed); `allow_private` relaxes `serverTarget`.
pub const Options = struct { allow_loopback: bool = false, allow_private: bool = false };

const Cidr = struct {
    addr: Addr,
    bits: u8,

    fn contains(self: Cidr, a: Addr) bool {
        if (self.addr.v6 != a.v6) return false;
        var left: usize = self.bits;
        for (self.addr.slice(), a.slice()) |x, y| {
            if (left == 0) return true;
            if (left < 8) {
                const mask = @as(u8, 0xff) << @intCast(8 - left);
                return (x ^ y) & mask == 0;
            }
            if (x != y) return false;
            left -= 8;
        }
        return true;
    }
};

const Embed = struct { prefix: Cidr, offset: usize, except: []const Cidr };
const Rule = struct { classes: []const []const Cidr, unless: ?[]const u8 };

const Table = struct {
    embeds: []const Embed,
    fetch: []const Rule,
    server_target: []const Rule,
};

fn parseCidr(text: []const u8) Cidr {
    const slash = std.mem.indexOfScalar(u8, text, '/') orelse @panic("blockedRanges.json: a CIDR without a length");
    const a = addr_mod.parseLiteral(text[0..slash]) orelse @panic("blockedRanges.json: a CIDR that is no address");
    return .{ .addr = a, .bits = std.fmt.parseInt(u8, text[slash + 1 ..], 10) catch @panic("blockedRanges.json: bad prefix length") };
}

fn cidrs(a: std.mem.Allocator, list: std.json.Value) ![]const Cidr {
    const out = try a.alloc(Cidr, list.array.items.len);
    for (list.array.items, out) |v, *c| c.* = parseCidr(v.string);
    return out;
}

fn rules(a: std.mem.Allocator, classes: std.json.ObjectMap, policy: std.json.ObjectMap) ![]const Rule {
    var out: std.ArrayList(Rule) = .empty;
    var named: std.ArrayList([]const Cidr) = .empty;
    for (policy.get("blocks").?.array.items) |name| try named.append(a, try cidrs(a, classes.get(name.string).?));
    try out.append(a, .{ .classes = try named.toOwnedSlice(a), .unless = null });
    if (policy.get("blocksUnless")) |unless| {
        var it = unless.object.iterator();
        while (it.next()) |kv| {
            var group: std.ArrayList([]const Cidr) = .empty;
            for (kv.value_ptr.array.items) |name| try group.append(a, try cidrs(a, classes.get(name.string).?));
            try out.append(a, .{ .classes = try group.toOwnedSlice(a), .unless = kv.key_ptr.* });
        }
    }
    return out.toOwnedSlice(a);
}

fn build(a: std.mem.Allocator) !Table {
    const root = (try std.json.parseFromSliceLeaky(std.json.Value, a, table_json, .{})).object;
    const classes = root.get("classes").?.object;
    var embeds: std.ArrayList(Embed) = .empty;
    for (root.get("embedsV4").?.array.items) |e| {
        const except = if (e.object.get("except")) |x| try cidrs(a, x) else &.{};
        try embeds.append(a, .{ .prefix = parseCidr(e.object.get("prefix").?.string), .offset = @intCast(e.object.get("offset").?.integer), .except = except });
    }
    const policies = root.get("policies").?.object;
    return .{
        .embeds = try embeds.toOwnedSlice(a),
        .fetch = try rules(a, classes, policies.get("fetch").?.object),
        .server_target = try rules(a, classes, policies.get("serverTarget").?.object),
    };
}

// One table per thread (the scrape fetch pool judges hosts off the main thread), read once
// from the embedded JSON and kept for the thread's life.
threadlocal var cached: ?Table = null;

fn table() *const Table {
    if (cached == null) {
        var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
        cached = build(arena.allocator()) catch @panic("blockedRanges.json is malformed");
    }
    return &cached.?;
}

/// The address a guard judges: an IPv6 address inside an embedsV4 prefix (outside its `except`)
/// is the IPv4 address it carries.
pub fn judged(a: Addr) Addr {
    if (!a.v6) return a;
    for (table().embeds) |e| {
        if (!e.prefix.contains(a)) continue;
        for (e.except) |x| if (x.contains(a)) return a;
        return Addr.v4(a.bytes[e.offset..][0..4].*);
    }
    return a;
}

fn inAny(classes: []const []const Cidr, a: Addr) bool {
    for (classes) |class| for (class) |c| if (c.contains(a)) return true;
    return false;
}

/// True when `policy` refuses `raw` under `opts`.
pub fn blocked(raw: Addr, policy: Policy, opts: Options) bool {
    const a = judged(raw);
    const list = switch (policy) {
        .fetch => table().fetch,
        .server_target => table().server_target,
    };
    for (list) |r| {
        if (r.unless) |option| {
            const relaxed = (std.mem.eql(u8, option, "allowLoopback") and opts.allow_loopback) or
                (std.mem.eql(u8, option, "allowPrivate") and opts.allow_private);
            if (relaxed) continue;
        }
        if (inAny(r.classes, a)) return true;
    }
    return false;
}

/// True for an address in the table's `loopback` class, carried or not.
pub fn isLoopback(raw: Addr) bool {
    const a = judged(raw);
    return blocked(a, .fetch, .{}) and !blocked(a, .fetch, .{ .allow_loopback = true });
}

const testing = std.testing;

test "ranges: a carried IPv4 is judged as itself, the except list is not" {
    const lit = addr_mod.parseLiteral;
    try testing.expect(blocked(lit("::ffff:169.254.169.254").?, .fetch, .{ .allow_loopback = true }));
    try testing.expect(blocked(lit("64:ff9b::a00:5").?, .fetch, .{}));
    try testing.expect(!blocked(lit("64:ff9b::a00:5").?, .server_target, .{ .allow_private = true }));
    try testing.expect(blocked(lit("2002:a9fe:a9fe::1").?, .server_target, .{ .allow_private = true }));
    try testing.expect(!blocked(lit("2002:808:808::1").?, .fetch, .{}));
    try testing.expect(isLoopback(lit("::1").?) and isLoopback(lit("::127.0.0.1").?) and !isLoopback(lit("::2").?));
}

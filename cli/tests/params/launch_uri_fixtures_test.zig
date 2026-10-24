// Walks the shared launch-URI corpus (common/fixtures/deepLink/launchQuery.json, format in its
// _schema.md) through src/params/uri.zig: every vector's URL parts parse to its expectation, the
// fields it leaves out being the empty query's.
const std = @import("std");
const uri = @import("../../src/params/uri.zig");
const fx = @import("../fixture_corpus.zig");
const testing = std.testing;

const Value = std.json.Value;

fn str(v: ?Value) ?[]const u8 {
    const x = v orelse return null;
    return if (x == .string) x.string else null;
}
fn num(v: ?Value) ?f64 {
    const x = v orelse return null;
    return switch (x) {
        .integer => |i| @floatFromInt(i),
        .float => |f| f,
        else => null,
    };
}
fn sameStr(got: ?[]const u8, want: ?[]const u8) bool {
    if (got == null or want == null) return got == null and want == null;
    return std.mem.eql(u8, got.?, want.?);
}
fn sameNum(got: ?f64, want: ?f64) bool {
    if (got == null or want == null) return got == null and want == null;
    return got.? == want.?;
}
fn jsonEq(a: Value, b: Value) bool {
    if (@as(std.meta.Tag(Value), a) != @as(std.meta.Tag(Value), b)) return sameNum(num(a), num(b));
    return switch (a) {
        .null => true,
        .bool => |x| x == b.bool,
        .integer, .float => sameNum(num(a), num(b)),
        .number_string, .string => std.mem.eql(u8, a.string, b.string),
        .array => |arr| arr.items.len == b.array.items.len and blk: {
            for (arr.items, b.array.items) |x, y| if (!jsonEq(x, y)) break :blk false;
            break :blk true;
        },
        .object => |obj| obj.count() == b.object.count() and blk: {
            var it = obj.iterator();
            while (it.next()) |e| {
                const other = b.object.get(e.key_ptr.*) orelse break :blk false;
                if (!jsonEq(e.value_ptr.*, other)) break :blk false;
            }
            break :blk true;
        },
    };
}

/// The vector's field, or the empty query's value for it.
fn field(expect: ?Value, key: []const u8, empty: Value) Value {
    const e = expect orelse return empty;
    return e.object.get(key) orelse empty;
}

fn checkPicture(w: *fx.Walk, name: []const u8, got: ?uri.Picture, want: Value) void {
    if (want == .null) {
        if (got != null) w.fail("{s}: expected no picture\n", .{name});
        return;
    }
    const p = got orelse return w.fail("{s}: expected a picture\n", .{name});
    const kind = str(want.object.get("kind")).?;
    const ok = switch (p) {
        .server => |s| std.mem.eql(u8, kind, "server") and sameStr(s.url, str(want.object.get("url"))) and
            sameStr(s.id, str(want.object.get("id"))) and sameNum(s.version, num(want.object.get("version"))),
        .project => |pr| std.mem.eql(u8, kind, "project") and sameStr(pr.server, str(want.object.get("server"))) and
            sameStr(pr.ref, str(want.object.get("ref"))),
        .source => |s| std.mem.eql(u8, kind, "source") and sameStr(s.url, str(want.object.get("url"))) and
            sameNum(s.frame, num(want.object.get("frame"))),
        .blank => |b| std.mem.eql(u8, kind, "blank") and sameStr(b.color, str(want.object.get("color"))) and
            sameNum(b.width, num(want.object.get("width"))) and sameNum(b.height, num(want.object.get("height"))),
    };
    if (!ok) w.fail("{s}: picture differs\n", .{name});
}

fn checkLayout(a: std.mem.Allocator, w: *fx.Walk, name: []const u8, got: ?uri.Layout, want: Value) void {
    if (want == .null) {
        if (got != null) w.fail("{s}: expected no layout\n", .{name});
        return;
    }
    const l = got orelse return w.fail("{s}: expected a layout\n", .{name});
    switch (l) {
        .url => |u| if (!sameStr(u, str(want.object.get("url")))) w.fail("{s}: layout url differs\n", .{name}),
        .json => |j| {
            const data = want.object.get("data") orelse return w.fail("{s}: expected a layout url\n", .{name});
            const parsed = std.json.parseFromSliceLeaky(Value, a, j, .{}) catch return w.fail("{s}: inline layout unparsable\n", .{name});
            if (!jsonEq(parsed, data)) w.fail("{s}: inline layout differs\n", .{name});
        },
    }
}

fn checkSettings(w: *fx.Walk, name: []const u8, got: []uri.Setting, want: Value) void {
    if (want.object.count() != got.len) return w.fail("{s}: {d} settings, expected {d}\n", .{ name, got.len, want.object.count() });
    for (got) |s| {
        const v = want.object.get(s.key) orelse return w.fail("{s}: unexpected setting {s}\n", .{ name, s.key });
        const ok = switch (s.value) {
            .text => |t| sameStr(t, str(v)),
            .number => |n| sameNum(n, num(v)),
            .flag => |f| v == .bool and v.bool == f,
        };
        if (!ok) w.fail("{s}: setting {s} differs\n", .{ name, s.key });
    }
}

fn checkPairs(w: *fx.Walk, name: []const u8, what: []const u8, got_len: usize, want: Value, eq: anytype) void {
    if (want.array.items.len != got_len) return w.fail("{s}: {d} {s}, expected {d}\n", .{ name, got_len, what, want.array.items.len });
    for (want.array.items, 0..) |v, i| if (!eq.eq(i, v)) w.fail("{s}: {s} {d} differs\n", .{ name, what, i });
}

test "launchQuery.json: every vector parses to its expectation" {
    var w = fx.Walk.start();
    defer w.stop();
    const a = w.alloc();
    const cases = try w.cases("deepLink/launchQuery.json");
    try testing.expect(cases.len >= 20);
    const empty_obj = Value{ .object = try std.json.ObjectMap.init(a, &.{}, &.{}) };
    const empty_arr = Value{ .array = std.json.Array.init(a) };
    for (cases) |c| {
        const name = fx.memberStr(c, "name").?;
        w.walked += 1;
        const search = fx.memberStr(c, "search") orelse "";
        const hash = fx.memberStr(c, "hash") orelse "";
        const got = try uri.parseParts(a, search, hash);
        const expect = fx.member(c, "expect").?;
        if (expect == .null) {
            if (got != null) w.fail("{s}: expected null\n", .{name});
            continue;
        }
        const q = got orelse {
            w.fail("{s}: expected a query, got null\n", .{name});
            continue;
        };
        checkPicture(&w, name, q.picture, field(expect, "picture", .null));
        if (!sameStr(q.name, str(field(expect, "name", .null)))) w.fail("{s}: name differs\n", .{name});
        checkLayout(a, &w, name, q.layout, field(expect, "layout", .null));
        if (q.incognito != field(expect, "incognito", .{ .bool = false }).bool) w.fail("{s}: incognito differs\n", .{name});
        if (!std.mem.eql(u8, q.script, str(field(expect, "script", .{ .string = "" })).?)) w.fail("{s}: script differs\n", .{name});
        if (!sameStr(q.theme, str(field(expect, "theme", .null)))) w.fail("{s}: theme differs\n", .{name});
        checkSettings(&w, name, q.settings, field(expect, "settings", empty_obj));
        const llm = field(expect, "llm", empty_obj).object;
        if (!sameStr(q.llm.provider, str(llm.get("provider"))) or !sameStr(q.llm.model, str(llm.get("model"))) or
            !sameStr(q.llm.base_url, str(llm.get("baseUrl"))) or !sameStr(q.llm.server_url, str(llm.get("serverUrl"))) or
            !sameStr(q.llm.api_key, str(llm.get("apiKey"))) or llm.count() > 5) w.fail("{s}: llm differs\n", .{name});
        const chat = field(expect, "chat", .null);
        const chat_open = if (chat == .null) false else chat.object.get("open").?.bool;
        const chat_dock = if (chat == .null) null else str(chat.object.get("dock"));
        if (q.chat_open != chat_open or !sameStr(q.chat_dock, chat_dock)) w.fail("{s}: chat differs\n", .{name});
        const connects = q.connect;
        checkPairs(&w, name, "connect", connects.len, field(expect, "connect", empty_arr), struct {
            list: []uri.Connect,
            fn eq(self: @This(), i: usize, v: Value) bool {
                return sameStr(self.list[i].url, str(v.object.get("url"))) and sameStr(self.list[i].token, str(v.object.get("token")));
            }
        }{ .list = connects });
        const project = field(expect, "project", .null);
        const pf = struct {
            fn get(p: Value, key: []const u8) ?[]const u8 {
                return if (p == .null) null else str(p.object.get(key));
            }
        };
        if (!sameStr(q.description, pf.get(project, "description")) or !sameStr(q.keywords, pf.get(project, "keywords")) or
            !sameStr(q.color, pf.get(project, "color")) or !sameStr(q.expire, pf.get(project, "expire"))) w.fail("{s}: project differs\n", .{name});
        const warnings = q.warnings;
        checkPairs(&w, name, "warning", warnings.len, field(expect, "warnings", empty_arr), struct {
            list: []uri.Warning,
            fn eq(self: @This(), i: usize, v: Value) bool {
                return sameStr(self.list[i].code.word(), str(v.object.get("code"))) and sameStr(self.list[i].key, str(v.object.get("key")));
            }
        }{ .list = warnings });
        if (q.from_hash != field(expect, "fromHash", .{ .bool = false }).bool) w.fail("{s}: fromHash differs\n", .{name});
        if (q.opens_session != field(expect, "opensSession", .{ .bool = false }).bool) w.fail("{s}: opensSession differs\n", .{name});
    }
    try w.report("launchQuery");
}

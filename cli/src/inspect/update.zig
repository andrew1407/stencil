//! `--project-update <id>`: the `--set-*` fields written onto a server project in one
//! version-guarded `PUT /projects/{id}`, then the project's public metadata printed as
//! `--project-info` prints it. The guard is `--if-version`, else the version the project has now.
const std = @import("std");
const args = @import("../args.zig");
const core = @import("../core.zig");
const server = @import("../server/client.zig");
const report = @import("../app/report.zig");
const projects = @import("projects.zig");

pub fn run(gpa: std.mem.Allocator, io: std.Io, out: *std.Io.Writer, opts: args.Options, id: []const u8) !void {
    var client = try projects.connect(gpa, io, opts, "--project-update");
    defer client.deinit();
    const record = update(gpa, &client, id, opts.set) catch |e| {
        reportFailure(client.base, id, e);
        return e;
    };
    defer gpa.free(record);
    var parsed = std.json.parseFromSlice(std.json.Value, gpa, record, .{}) catch {
        reportFailure(client.base, id, server.Error.BadResponse);
        return server.Error.BadResponse;
    };
    defer parsed.deinit();
    if (parsed.value != .object) {
        reportFailure(client.base, id, server.Error.BadResponse);
        return server.Error.BadResponse;
    }
    var js: std.json.Stringify = .{ .writer = out };
    try projects.writeMeta(&js, parsed.value.object);
    try out.writeByte('\n');
    try out.flush();
}

/// PUT `set` onto project `id` and return the record the server answers with.
pub fn update(gpa: std.mem.Allocator, client: *server.Client, id: []const u8, set: args.ProjectSet) ![]u8 {
    const version = set.if_version orelse try client.getProjectVersion(id);
    const payload = try body(gpa, set, version);
    defer gpa.free(payload);
    const path = try std.fmt.allocPrint(gpa, "/projects/{s}", .{id});
    defer gpa.free(path);
    return client.request(.PUT, path, payload, "application/json");
}

/// The PUT body, as server/internal/protocol UpdateProjectRequest reads it: only the fields given,
/// a colour normalized to `#rrggbb`, the keywords trimmed with the empty ones dropped.
pub fn body(gpa: std.mem.Allocator, set: args.ProjectSet, version: i64) ![]u8 {
    var doc: std.Io.Writer.Allocating = .init(gpa);
    errdefer doc.deinit();
    var js: std.json.Stringify = .{ .writer = &doc.writer };
    try js.beginObject();
    if (set.name) |v| try field(&js, "name", v);
    if (set.description) |v| try field(&js, "description", v);
    if (set.keywords) |list| {
        try js.objectField("keywords");
        try js.beginArray();
        var it = std.mem.splitScalar(u8, list, ',');
        while (it.next()) |k| {
            const word = std.mem.trim(u8, k, " \t");
            if (word.len != 0) try js.write(word);
        }
        try js.endArray();
    }
    var hex: [7]u8 = undefined;
    if (set.color) |v| try field(&js, "color", try normalize(v, &hex));
    if (set.blank_color) |v| try field(&js, "blankColor", try normalize(v, &hex));
    if (set.expires) |v| try field(&js, "expiresAt", v);
    try field(&js, "version", version);
    try js.endObject();
    return doc.toOwnedSlice();
}

fn field(js: *std.json.Stringify, name: []const u8, value: anytype) !void {
    try js.objectField(name);
    try js.write(value);
}

/// "" stays a clear; anything else core parses becomes `#rrggbb`, as the console stores it.
fn normalize(spec: [:0]const u8, buf: *[7]u8) ![]const u8 {
    if (spec.len == 0) return "";
    const c = core.parseColor(spec) orelse return error.BadColor;
    return std.fmt.bufPrint(buf, "#{x:0>2}{x:0>2}{x:0>2}", .{ c.r, c.g, c.b });
}

fn reportFailure(base: []const u8, id: []const u8, e: anyerror) void {
    if (server.lastReject()) |r| return report.err("{s} rejected the update of {s} ({d}): {s}\n", .{ base, id, r.status, r.message });
    switch (e) {
        error.BadResponse => report.err("{s} answered the update of {s} with something that is not a project\n", .{ base, id }),
        else => report.err("could not update {s} on {s} ({s})\n", .{ id, base, @errorName(e) }),
    }
}

const testing = std.testing;

/// Records each request and answers from a script: the project's GET, then the PUT's record.
const Put = struct {
    var methods: [2]std.http.Method = undefined;
    var targets: [2][64]u8 = undefined;
    var sent: [256]u8 = undefined;
    var sent_len: usize = 0;
    var n: usize = 0;

    fn run(gpa: std.mem.Allocator, _: std.Io, target: []const u8, method: std.http.Method, payload: ?[]const u8, _: []const std.http.Header) server.TransportError![]u8 {
        defer n += 1;
        methods[n] = method;
        @memset(&targets[n], 0);
        @memcpy(targets[n][0..@min(target.len, 63)], target[0..@min(target.len, 63)]);
        if (payload) |p| {
            sent_len = @min(p.len, sent.len);
            @memcpy(sent[0..sent_len], p[0..sent_len]);
        }
        if (method == .GET) return gpa.dupe(u8, "{\"project\":{\"id\":\"p_1_a\",\"version\":4},\"layout\":null}");
        return gpa.dupe(u8, "{\"id\":\"p_1_a\",\"name\":\"Plans v2\",\"version\":5,\"ownerSession\":\"s\"}");
    }
};

fn putClient() !server.Client {
    const a = testing.allocator;
    Put.n = 0;
    Put.sent_len = 0;
    return .{
        .gpa = a,
        .io = undefined, // the scripted transport never touches it
        .base = try a.dupe(u8, "http://s"),
        .token = try a.dupe(u8, "t"),
        .auth = try a.dupe(u8, "Bearer t"),
        .credential = try a.dupe(u8, ""),
        .transport = Put.run,
    };
}

test "body: only the fields given, a colour normalized, the keywords trimmed, '' kept as a clear" {
    const a = testing.allocator;
    const got = try body(a, .{ .name = "Plans \"v2\"", .keywords = " a, ,b ", .color = "red", .blank_color = "", .expires = 0 }, 4);
    defer a.free(got);
    try testing.expectEqualStrings(
        \\{"name":"Plans \"v2\"","keywords":["a","b"],"color":"#ff0000","blankColor":"","expiresAt":0,"version":4}
    , got);
    const cleared = try body(a, .{ .description = "", .keywords = "" }, 9);
    defer a.free(cleared);
    try testing.expectEqualStrings("{\"description\":\"\",\"keywords\":[],\"version\":9}", cleared);
}

test "update: guarded by the project's current version, or by --if-version without a read" {
    const a = testing.allocator;
    var c = try putClient();
    defer c.deinit();
    const record = try update(a, &c, "p_1_a", .{ .name = "Plans v2" });
    defer a.free(record);
    try testing.expectEqual(@as(usize, 2), Put.n);
    try testing.expectEqual(std.http.Method.GET, Put.methods[0]);
    try testing.expectEqual(std.http.Method.PUT, Put.methods[1]);
    try testing.expectEqualStrings("http://s/projects/p_1_a", std.mem.sliceTo(&Put.targets[1], 0));
    try testing.expectEqualStrings("{\"name\":\"Plans v2\",\"version\":4}", Put.sent[0..Put.sent_len]);
    try testing.expect(std.mem.indexOf(u8, record, "\"version\":5") != null);

    var d = try putClient();
    defer d.deinit();
    const guarded = try update(a, &d, "p_1_a", .{ .description = "d", .if_version = 2 });
    defer a.free(guarded);
    try testing.expectEqual(@as(usize, 1), Put.n);
    try testing.expectEqualStrings("{\"description\":\"d\",\"version\":2}", Put.sent[0..Put.sent_len]);
}

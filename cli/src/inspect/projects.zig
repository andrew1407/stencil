//! `--list-projects` and `--project-info <id|name>`: every page of `GET /projects`, following
//! `nextCursor`, each project cut down to its public metadata — never the stored image, the
//! layout or the server's own file paths. Nothing reaches stdout until the walk has finished.
const std = @import("std");
const args = @import("../args.zig");
const server = @import("../server/client.zig");
const report = @import("../app/report.zig");

/// server/internal/protocol ProjectRecord without its storage fields and payloads.
const meta_keys = [_][]const u8{
    "id",     "name",     "createdAt", "updatedAt",   "expiresAt", "hasImage",   "imageW", "imageH",       "version",
    "source", "resource", "color",     "description", "keywords",  "blankColor", "blank",  "originalHash",
};

/// The server's cap on `?limit=` (server/internal/validate MaxListLimit).
const page_size = 500;
/// A server that keeps handing out cursors is cut off here rather than followed forever.
const max_pages = 1000;

pub fn run(gpa: std.mem.Allocator, io: std.Io, out: *std.Io.Writer, opts: args.Options, key: ?[]const u8) !void {
    var client = try connect(gpa, io, opts, if (key == null) "--list-projects" else "--project-info");
    defer client.deinit();

    var doc: std.Io.Writer.Allocating = .init(gpa);
    defer doc.deinit();
    var js: std.json.Stringify = .{ .writer = &doc.writer };
    const found = walk(gpa, &client, &js, key) catch |e| {
        reportFailure(client.base, e);
        return e;
    };
    if (key) |k| if (!found) {
        report.err("no server project with the id or name \"{s}\"\n", .{k});
        return error.NotFound;
    };
    try out.writeAll(doc.written());
    try out.writeByte('\n');
    try out.flush();
}

/// Dial `--server` as every server mode does (§1 Server credentials), or say why not.
pub fn connect(gpa: std.mem.Allocator, io: std.Io, opts: args.Options, mode: []const u8) !server.Client {
    const url = opts.server orelse {
        report.err("{s} needs --server <url>\n", .{mode});
        return error.NoServer;
    };
    const token = server.tokenFor(opts.token, opts.env_tokens.per_origin, opts.env_tokens.single, url);
    return server.connect(gpa, io, url, token) catch |e| {
        server.printConnectError(url, e);
        return e;
    };
}

/// Write every project as an array, or with `key` only the first whose id — or name, ignoring
/// case — it is. Returns whether `key` was found.
pub fn walk(gpa: std.mem.Allocator, client: *server.Client, js: *std.json.Stringify, key: ?[]const u8) !bool {
    if (key == null) try js.beginArray();
    var cursor: ?[]u8 = null;
    defer if (cursor) |c| gpa.free(c);
    for (0..max_pages) |_| {
        const path = try pagePath(gpa, cursor);
        defer gpa.free(path);
        const body = try client.request(.GET, path, null, null);
        defer gpa.free(body);
        var parsed = std.json.parseFromSlice(std.json.Value, gpa, body, .{}) catch return server.Error.BadResponse;
        defer parsed.deinit();
        if (parsed.value != .object) return server.Error.BadResponse;
        const list = parsed.value.object.get("projects") orelse return server.Error.BadResponse;
        if (list != .array) return server.Error.BadResponse;
        for (list.array.items) |p| {
            if (p != .object) continue;
            if (key) |k| {
                if (!matches(p.object, k)) continue;
                try writeMeta(js, p.object);
                return true;
            }
            try writeMeta(js, p.object);
        }
        const next = parsed.value.object.get("nextCursor") orelse break;
        if (next != .string or next.string.len == 0) break;
        if (cursor) |c| {
            if (std.mem.eql(u8, c, next.string)) return error.CursorLoop;
            gpa.free(c);
            cursor = null;
        }
        cursor = try gpa.dupe(u8, next.string);
    } else return error.TooManyPages;
    if (key == null) try js.endArray();
    return false;
}

fn matches(p: std.json.ObjectMap, key: []const u8) bool {
    const id = p.get("id") orelse return false;
    if (id == .string and std.mem.eql(u8, id.string, key)) return true;
    const name = p.get("name") orelse return false;
    return name == .string and std.ascii.eqlIgnoreCase(name.string, key);
}

pub fn writeMeta(js: *std.json.Stringify, p: std.json.ObjectMap) !void {
    try js.beginObject();
    try writeFields(js, p);
    try js.endObject();
}

/// `p`'s public metadata, in `meta_keys` order, into an object the caller has begun.
pub fn writeFields(js: *std.json.Stringify, p: std.json.ObjectMap) !void {
    for (meta_keys) |k| {
        const v = p.get(k) orelse continue;
        try js.objectField(k);
        try js.write(v);
    }
}

/// `/projects?limit=N`, plus the cursor percent-encoded byte by byte, unreserved kept.
fn pagePath(gpa: std.mem.Allocator, cursor: ?[]const u8) ![]u8 {
    var path: std.Io.Writer.Allocating = .init(gpa);
    errdefer path.deinit();
    try path.writer.print("/projects?limit={d}", .{page_size});
    if (cursor) |c| {
        try path.writer.writeAll("&after=");
        for (c) |b| {
            if (std.ascii.isAlphanumeric(b) or std.mem.indexOfScalar(u8, "-_.~", b) != null) {
                try path.writer.writeByte(b);
            } else try path.writer.print("%{X:0>2}", .{b});
        }
    }
    return path.toOwnedSlice();
}

fn reportFailure(base: []const u8, e: anyerror) void {
    if (server.lastReject()) |r| return report.err("{s} rejected the listing ({d}): {s}\n", .{ base, r.status, r.message });
    switch (e) {
        error.CursorLoop => report.err("{s} handed back the same page cursor twice\n", .{base}),
        error.TooManyPages => report.err("{s} kept paging past {d} pages\n", .{ base, max_pages }),
        error.BadResponse => report.err("{s} answered with something that is not a project list\n", .{base}),
        else => report.err("could not list projects on {s} ({s})\n", .{ base, @errorName(e) }),
    }
}

const testing = std.testing;

/// Answers each request with the next scripted body, keeping every URL it was asked for.
const Pages = struct {
    var bodies: []const []const u8 = &.{};
    var asked: [4][96]u8 = undefined;
    var n: usize = 0;

    fn run(gpa: std.mem.Allocator, _: std.Io, target: []const u8, _: std.http.Method, _: ?[]const u8, _: []const std.http.Header) server.TransportError![]u8 {
        const at = @min(target.len, 95);
        @memcpy(asked[n][0..at], target[0..at]);
        asked[n][at] = 0;
        defer n += 1;
        return gpa.dupe(u8, bodies[@min(n, bodies.len - 1)]);
    }

    fn askedFor(i: usize) []const u8 {
        return std.mem.sliceTo(&asked[i], 0);
    }
};

fn walked(bodies: []const []const u8, key: ?[]const u8, out: *std.Io.Writer.Allocating) !bool {
    const a = testing.allocator;
    Pages.bodies = bodies;
    Pages.n = 0;
    var c = server.Client{
        .gpa = a,
        .io = undefined, // the scripted transport never touches it
        .base = try a.dupe(u8, "http://s"),
        .token = try a.dupe(u8, "t"),
        .auth = try a.dupe(u8, "Bearer t"),
        .credential = try a.dupe(u8, ""),
        .transport = Pages.run,
    };
    defer c.deinit();
    var js: std.json.Stringify = .{ .writer = &out.writer };
    return walk(a, &c, &js, key);
}

const page_one =
    \\{"projects":[{"id":"p_1_a","name":"Plans","imageW":8,"imageH":6,"version":3,"layout":{"lines":[]},
    \\"originalContent":"data:image/png;base64,AAAA","ownerSession":"s","resultPath":"x/y.png"}],"nextCursor":"1700/p_1_a"}
;
const page_two =
    \\{"projects":[{"id":"p_2_b","name":"Shots","keywords":["k"],"hasImage":true}]}
;

test "walk: every page in order, metadata only, the cursor sent back encoded" {
    var out: std.Io.Writer.Allocating = .init(testing.allocator);
    defer out.deinit();
    try testing.expect(!try walked(&.{ page_one, page_two }, null, &out));
    try testing.expectEqualStrings(
        \\[{"id":"p_1_a","name":"Plans","imageW":8,"imageH":6,"version":3},{"id":"p_2_b","name":"Shots","hasImage":true,"keywords":["k"]}]
    , out.written());
    try testing.expectEqual(@as(usize, 2), Pages.n);
    try testing.expectEqualStrings("http://s/projects?limit=500", Pages.askedFor(0));
    try testing.expectEqualStrings("http://s/projects?limit=500&after=1700%2Fp_1_a", Pages.askedFor(1));
}

test "walk: a key finds one project by id or by name, and stops paging there" {
    var out: std.Io.Writer.Allocating = .init(testing.allocator);
    defer out.deinit();
    try testing.expect(try walked(&.{ page_one, page_two }, "p_1_a", &out));
    try testing.expectEqual(@as(usize, 1), Pages.n);
    try testing.expect(std.mem.startsWith(u8, out.written(), "{\"id\":\"p_1_a\""));

    out.clearRetainingCapacity();
    try testing.expect(try walked(&.{ page_one, page_two }, "shots", &out));
    try testing.expect(std.mem.startsWith(u8, out.written(), "{\"id\":\"p_2_b\""));

    out.clearRetainingCapacity();
    try testing.expect(!try walked(&.{ page_one, page_two }, "nope", &out));
    try testing.expectEqualStrings("", out.written());
}

test "walk: a repeated cursor and a body that is no list are errors, not loops" {
    var out: std.Io.Writer.Allocating = .init(testing.allocator);
    defer out.deinit();
    try testing.expectError(error.CursorLoop, walked(&.{ page_one, page_one }, null, &out));
    try testing.expectError(server.Error.BadResponse, walked(&.{"{\"nope\":1}"}, null, &out));
}

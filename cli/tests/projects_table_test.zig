// gatherRows: one server's ProjectInfo list mapped to rendered rows, over a faked HTTP
// transport (no network). tests/pins/projects*.txt pin how the rows LOOK; this pins what
// goes into them, and the listing failure that must not take the other servers down.
const std = @import("std");
const server = @import("../src/serverClient.zig");
const projectsTable = @import("../src/console/projectsTable.zig");
const logo = @import("../src/logo.zig");
const testing = std.testing;

const now_ms: i64 = 1_700_000_000_000;
const day: i64 = 24 * 60 * 60 * 1000;

const list_body =
    \\{"projects":[
    \\{"name":"poster","imageW":1200,"imageH":800,"createdAt":1699740800000,"updatedAt":1699999700000,"expiresAt":0,"color":"#ff8800","description":"the wall poster"},
    \\{"name":"unrendered","imageW":0,"imageH":0,"createdAt":1699999700000,"updatedAt":1699999700000,"expiresAt":1700259200000,"color":"","description":""}
    \\]}
;

fn okTransport(gpa: std.mem.Allocator, _: std.Io, _: []const u8, _: std.http.Method, _: ?[]const u8, _: []const std.http.Header) server.TransportError![]u8 {
    return gpa.dupe(u8, list_body);
}

fn failTransport(_: std.mem.Allocator, _: std.Io, _: []const u8, _: std.http.Method, _: ?[]const u8, _: []const std.http.Header) server.TransportError![]u8 {
    return server.Error.HttpFailed;
}

fn fakeClient(a: std.mem.Allocator, io: std.Io, transport: server.Transport) !server.Client {
    return .{
        .gpa = a,
        .io = io,
        .base = try a.dupe(u8, "http://localhost:8090"),
        .token = try a.dupe(u8, "t"),
        .auth = try a.dupe(u8, "Bearer t"),
        .credential = try a.dupe(u8, ""),
        .transport = transport,
    };
}

test "gatherRows: a listing becomes rows, with the missing-size and single-server fallbacks" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();

    var client = try fakeClient(a, threaded.io(), okTransport);
    defer client.deinit();

    var rows: std.ArrayList(projectsTable.ProjectRow) = .empty;
    defer projectsTable.freeRows(a, &rows);
    try projectsTable.gatherRows(a, &rows, &client, now_ms, false);
    try testing.expectEqual(@as(usize, 2), rows.items.len);

    try testing.expectEqualStrings("poster", rows.items[0].name);
    try testing.expectEqualStrings("1200x800", rows.items[0].size);
    try testing.expectEqualStrings("#ff8800", rows.items[0].color);
    try testing.expectEqualStrings("the wall poster", rows.items[0].description);
    try testing.expectEqualStrings("", rows.items[0].server); // single server: no SERVER column

    // No stored dimensions renders as "-", never "0x0"; the relative clocks read forward
    // for EXPIRES and backward for CREATED/CHANGED.
    var tb: [32]u8 = undefined;
    try testing.expectEqualStrings("-", rows.items[1].size);
    try testing.expectEqualStrings(server.formatAgo(&tb, now_ms, 1699999700000), rows.items[1].created);
    try testing.expectEqualStrings(server.formatUntil(&tb, now_ms, 1700259200000), rows.items[1].expires);
    try testing.expectEqualStrings(server.formatAgo(&tb, now_ms, 1699999700000), rows.items[1].changed);
    try testing.expectEqualStrings("never", rows.items[0].expires); // expiresAt 0
}

test "gatherRows: every row names its origin once more than one server is listed" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();

    var client = try fakeClient(a, threaded.io(), okTransport);
    defer client.deinit();

    var rows: std.ArrayList(projectsTable.ProjectRow) = .empty;
    defer projectsTable.freeRows(a, &rows);
    try projectsTable.gatherRows(a, &rows, &client, now_ms, true);
    for (rows.items) |r| try testing.expectEqualStrings("http://localhost:8090", r.server);
}

test "gatherRows: a server that cannot list is reported and skipped, not propagated" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();

    var bad = try fakeClient(a, threaded.io(), failTransport);
    defer bad.deinit();
    var good = try fakeClient(a, threaded.io(), okTransport);
    defer good.deinit();

    var rows: std.ArrayList(projectsTable.ProjectRow) = .empty;
    defer projectsTable.freeRows(a, &rows);
    try projectsTable.gatherRows(a, &rows, &bad, now_ms, true); // returns, does not fail
    try testing.expectEqual(@as(usize, 0), rows.items.len);
    try projectsTable.gatherRows(a, &rows, &good, now_ms, true);
    try testing.expectEqual(@as(usize, 2), rows.items.len);
}

// Collects renderTable output through logo.zig's sink seam.
const Cap = struct {
    buf: std.ArrayList(u8) = .empty,
    fn sink(ctx: *anyopaque, bytes: []const u8) void {
        const self: *Cap = @ptrCast(@alignCast(ctx));
        self.buf.appendSlice(testing.allocator, bytes) catch {};
    }
};

test "renderTable: a long multi-byte description is cut back to a codepoint boundary" {
    const a = testing.allocator;
    var cap = Cap{};
    defer cap.buf.deinit(a);
    logo.setSink(Cap.sink, &cap);
    defer logo.clearSink();
    defer logo.init(false, false);
    logo.init(true, false); // NO_COLOR: the note is plain text

    // 60 three-byte codepoints: the 48-byte cut lands mid-character unless backed off.
    const desc = "\u{4e00}" ** 60;
    const row = projectsTable.ProjectRow{
        .name = @constCast("p"),
        .size = @constCast("1x1"),
        .created = @constCast("1d"),
        .expires = @constCast("never"),
        .changed = @constCast("1d"),
        .color = @constCast(""),
        .description = @constCast(desc),
        .server = "",
    };
    projectsTable.renderTable(a, &.{row}, false);
    try testing.expect(std.unicode.utf8ValidateSlice(cap.buf.items));
    try testing.expect(std.mem.indexOf(u8, cap.buf.items, "…") != null); // truncated
}

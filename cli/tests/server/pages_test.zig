//! `Client.listProjects` over a server that pages `GET /projects`: every page merged in order into
//! one list, the cursor sent back encoded, and a cursor loop or an endless walk refused.
const std = @import("std");
const errors = @import("../../src/server/errors.zig");
const Client = @import("../../src/server/rest.zig").Client;
const ProjectPages = @import("../../src/server/parse.zig").ProjectPages;
const testing = std.testing;

/// Answers each request with the next scripted body (or a fresh cursor forever), keeping the URLs.
const Pages = struct {
    var bodies: []const []const u8 = &.{};
    var endless = false;
    var asked: [4][96]u8 = undefined;
    var n: usize = 0;

    fn run(gpa: std.mem.Allocator, _: std.Io, url: []const u8, _: std.http.Method, _: ?[]const u8, _: []const std.http.Header) errors.TransportError![]u8 {
        defer n += 1;
        if (n < asked.len) {
            const at = @min(url.len, 95);
            @memcpy(asked[n][0..at], url[0..at]);
            asked[n][at] = 0;
        }
        if (endless) return std.fmt.allocPrint(gpa, "{{\"projects\":[],\"nextCursor\":\"c{d}\"}}", .{n});
        return gpa.dupe(u8, bodies[@min(n, bodies.len - 1)]);
    }

    fn askedFor(i: usize) []const u8 {
        return std.mem.sliceTo(&asked[i], 0);
    }
};

fn client(bodies: []const []const u8) !Client {
    const a = testing.allocator;
    Pages.bodies = bodies;
    Pages.endless = false;
    Pages.n = 0;
    return .{
        .gpa = a,
        .io = undefined, // the scripted transport never touches it
        .base = try a.dupe(u8, "http://s"),
        .token = try a.dupe(u8, "t"),
        .auth = try a.dupe(u8, "Bearer t"),
        .credential = try a.dupe(u8, ""),
        .transport = Pages.run,
    };
}

const page_one =
    \\{"projects":[{"id":"p_1","name":"One","version":3}],"nextCursor":"1700/p_1"}
;
const page_two =
    \\{"projects":[{"id":"p_2","name":"Two"},{"id":"p_3","name":"Three"}],"nextCursor":"1600 p_3"}
;
const page_three =
    \\{"projects":[{"id":"p_4","name":"Four","version":9}]}
;

test "listProjects: every page merged in order, each cursor sent back encoded" {
    var c = try client(&.{ page_one, page_two, page_three });
    defer c.deinit();
    const body = try c.listProjects();
    defer testing.allocator.free(body);
    try testing.expectEqualStrings(
        \\{"projects":[{"id":"p_1","name":"One","version":3},{"id":"p_2","name":"Two"},{"id":"p_3","name":"Three"},{"id":"p_4","name":"Four","version":9}]}
    , body);
    try testing.expectEqual(@as(usize, 3), Pages.n);
    try testing.expectEqualStrings("http://s/projects", Pages.askedFor(0));
    try testing.expectEqualStrings("http://s/projects?after=1700%2Fp_1", Pages.askedFor(1));
    try testing.expectEqualStrings("http://s/projects?after=1600%20p_3", Pages.askedFor(2));
}

test "listProjects: a lookup by name reaches a project past the first page" {
    var c = try client(&.{ page_one, page_two, page_three });
    defer c.deinit();
    const ref = (try c.findProjectRef("four")).?;
    defer testing.allocator.free(ref.id);
    try testing.expectEqualStrings("p_4", ref.id);
    try testing.expectEqual(@as(i64, 9), ref.version);
}

test "listProjects: a lone page is one request and comes back byte for byte" {
    const lone = "{\"projects\": [{\"id\":\"p_1\"}], \"nextCursor\": \"\"}\n";
    for ([_][]const u8{ lone, "{\"projects\":[]}\n", "not json" }) |sent| {
        var c = try client(&.{sent});
        defer c.deinit();
        const body = try c.listProjects();
        defer testing.allocator.free(body);
        try testing.expectEqualStrings(sent, body);
        try testing.expectEqual(@as(usize, 1), Pages.n);
    }
}

test "listProjects: a cursor handed back again is a loop, not a walk" {
    const back_to_one =
        \\{"projects":[{"id":"p_9"}],"nextCursor":"1700/p_1"}
    ;
    var c = try client(&.{ page_one, page_two, back_to_one });
    defer c.deinit();
    try testing.expectError(error.CursorLoop, c.listProjects());
    try testing.expectEqual(@as(usize, 3), Pages.n);

    var d = try client(&.{ page_one, page_one });
    defer d.deinit();
    try testing.expectError(error.CursorLoop, d.listProjects());
}

test "listProjects: a page past the first that is no list is a bad response" {
    var c = try client(&.{ page_one, "{\"nope\":1}" });
    defer c.deinit();
    try testing.expectError(errors.Error.BadResponse, c.listProjects());
}

test "listProjects: a server that never stops paging is cut off at max_pages" {
    var c = try client(&.{});
    defer c.deinit();
    Pages.endless = true;
    try testing.expectError(error.TooManyPages, c.listProjects());
    try testing.expectEqual(@as(usize, ProjectPages.max_pages), Pages.n);
}

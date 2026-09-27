//! The server's JSON answers read back: tokens and project ids, the error body, a project found
//! by name, the owned project list and one project's string fields.
const std = @import("std");
const Error = @import("../../src/server/errors.zig").Error;
const parse = @import("../../src/server/parse.zig");
const findIdByName = parse.findIdByName;
const findProjectByName = parse.findProjectByName;
const freeProjectList = parse.freeProjectList;
const parseErrorMessage = parse.parseErrorMessage;
const parseProjectId = parse.parseProjectId;
const parseProjectList = parse.parseProjectList;
const parseProjectStringField = parse.parseProjectStringField;
const parseProjectVersion = parse.parseProjectVersion;
const parseToken = parse.parseToken;
const testing = std.testing;

test "parseToken / parseProjectId" {
    const a = testing.allocator;
    const tok = try parseToken(a, "{\"token\":\"abc123\",\"expiresAt\":0}");
    defer a.free(tok);
    try testing.expectEqualStrings("abc123", tok);

    const id = try parseProjectId(a, "{\"id\":\"p_x_y\",\"name\":\"N\",\"version\":0}");
    defer a.free(id);
    try testing.expectEqualStrings("p_x_y", id);
}

test "parseErrorMessage reads the server's error body, null for other shapes" {
    const a = testing.allocator;
    const msg = parseErrorMessage(a, "{\"code\":\"unauthorized\",\"message\":\"admin token required to issue tokens\"}").?;
    defer a.free(msg);
    try testing.expectEqualStrings("admin token required to issue tokens", msg);

    try testing.expect(parseErrorMessage(a, "not json") == null);
    try testing.expect(parseErrorMessage(a, "{\"code\":\"x\"}") == null); // no message field
}

test "findProjectByName captures id + version; parseProjectVersion reads a single project" {
    const a = testing.allocator;
    const list =
        "{\"projects\":[{\"id\":\"p_1_a\",\"name\":\"Alpha\",\"version\":4},{\"id\":\"p_2_b\",\"name\":\"Beta\",\"version\":9}]}";
    const ref = (try findProjectByName(a, list, "beta")).?;
    defer a.free(ref.id);
    try testing.expectEqualStrings("p_2_b", ref.id);
    try testing.expectEqual(@as(i64, 9), ref.version);
    try testing.expect((try findProjectByName(a, list, "missing")) == null);

    const v = try parseProjectVersion(a, "{\"project\":{\"id\":\"p_2_b\",\"name\":\"Beta\",\"version\":9}}");
    try testing.expectEqual(@as(i64, 9), v);
}

test "parseProjectList yields owned name/size/updatedAt/color records" {
    const a = testing.allocator;
    const body =
        "{\"projects\":[{\"id\":\"p1\",\"name\":\"Alpha\",\"imageW\":800,\"imageH\":600,\"updatedAt\":1700000000000,\"expiresAt\":1700009999000,\"color\":\"#ff5623\",\"description\":\"lead shot\"}," ++
        "{\"id\":\"p2\",\"name\":\"Beta\",\"imageW\":1024,\"imageH\":768,\"updatedAt\":0}]}";
    const items = try parseProjectList(a, body);
    defer freeProjectList(a, items);
    try testing.expectEqual(@as(usize, 2), items.len);
    try testing.expectEqualStrings("Alpha", items[0].name);
    try testing.expectEqual(@as(i64, 800), items[0].w);
    try testing.expectEqual(@as(i64, 600), items[0].h);
    try testing.expectEqual(@as(i64, 1700000000000), items[0].updated_at);
    try testing.expectEqual(@as(i64, 1700009999000), items[0].expires_at);
    try testing.expectEqualStrings("#ff5623", items[0].color);
    try testing.expectEqualStrings("lead shot", items[0].description);
    try testing.expectEqualStrings("Beta", items[1].name);
    try testing.expectEqual(@as(i64, 0), items[1].expires_at); // no expiry → 0 (never)
    try testing.expectEqualStrings("", items[1].color); // no custom colour → empty
    try testing.expectEqualStrings("", items[1].description); // no description → empty

    // An empty list parses to an empty (non-null) slice.
    const none = try parseProjectList(a, "{\"projects\":[]}");
    defer freeProjectList(a, none);
    try testing.expectEqual(@as(usize, 0), none.len);
}

test "parseProjectStringField reads one project field, empty when absent" {
    const a = testing.allocator;
    const body = "{\"project\":{\"id\":\"p_1\",\"name\":\"N\",\"color\":\"#7c3aed\",\"blankColor\":\"#fff\",\"description\":\"a caption\"}}";
    for ([_][2][]const u8{
        .{ "color", "#7c3aed" },
        .{ "blankColor", "#fff" },
        .{ "description", "a caption" },
    }) |c| {
        const got = try parseProjectStringField(a, body, c[0]);
        defer a.free(got);
        try testing.expectEqualStrings(c[1], got);
    }

    const none = try parseProjectStringField(a, "{\"project\":{\"id\":\"p_1\",\"name\":\"N\"}}", "color");
    defer a.free(none);
    try testing.expectEqualStrings("", none);

    try testing.expectError(Error.BadResponse, parseProjectStringField(a, "{\"nope\":1}", "color"));
}

test "findIdByName matches case-insensitively, else null" {
    const a = testing.allocator;
    const body =
        "{\"projects\":[{\"id\":\"p_1_a\",\"name\":\"Alpha\"},{\"id\":\"p_2_b\",\"name\":\"Beta\"}]}";
    const id = (try findIdByName(a, body, "beta")).?;
    defer a.free(id);
    try testing.expectEqualStrings("p_2_b", id);

    try testing.expect((try findIdByName(a, body, "missing")) == null);
}

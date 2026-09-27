//! The console's argument parsers: a project's base name, a layout export target, `/blank`
//! dims and page formats, `/connect` url/token pairs and the crop `album` modifier.
const std = @import("std");
const commands = @import("../../src/console/commands.zig");
const layoutTarget = commands.layoutTarget;
const parseBlank = commands.parseBlank;
const parseConnectArgs = commands.parseConnectArgs;
const projectBaseName = commands.projectBaseName;
const stripAlbum = commands.stripAlbum;
const testing = std.testing;

test "projectBaseName: basename without extension, fallback to layout" {
    try testing.expectEqualStrings("photo", projectBaseName("photo.png"));
    try testing.expectEqualStrings("photo", projectBaseName("/a/b/photo.png"));
    try testing.expectEqualStrings("photo", projectBaseName("a\\b\\photo.png")); // Windows-style separators
    try testing.expectEqualStrings("photo.tar", projectBaseName("photo.tar.gz")); // drop only the last extension
    try testing.expectEqualStrings("blank", projectBaseName("blank"));
    try testing.expectEqualStrings("layout", projectBaseName(""));
    try testing.expectEqualStrings("layout", projectBaseName("/a/b/.png")); // empty base after dropping ext
}

test "layoutTarget: .json passthrough, directory join, bare default" {
    const a = testing.allocator;

    // Ends with .json (any case) → exactly that path.
    const t1 = try layoutTarget(a, "out.json", "photo");
    defer a.free(t1);
    try testing.expectEqualStrings("out.json", t1);
    const t1b = try layoutTarget(a, "Sub/Out.JSON", "photo");
    defer a.free(t1b);
    try testing.expectEqualStrings("Sub/Out.JSON", t1b);

    // Non-empty without .json → treated as a directory/prefix.
    const t2 = try layoutTarget(a, "dir", "photo");
    defer a.free(t2);
    try testing.expectEqualStrings("dir/photo.json", t2);
    const t2b = try layoutTarget(a, "dir/", "photo"); // no doubled slash
    defer a.free(t2b);
    try testing.expectEqualStrings("dir/photo.json", t2b);

    // Empty → "<name>.json" in the cwd.
    const t3 = try layoutTarget(a, "", "photo");
    defer a.free(t3);
    try testing.expectEqualStrings("photo.json", t3);
}

test "parseBlank: dims, colour, and rejects" {
    const b1 = parseBlank("800 600 red").?;
    try testing.expectEqual(@as(u32, 800), b1.width.?);
    try testing.expectEqual(@as(u32, 600), b1.height.?);
    try testing.expectEqualStrings("red", b1.color);

    const b2 = parseBlank("").?;
    try testing.expect(b2.width == null);
    try testing.expectEqualStrings("white", b2.color);

    const b3 = parseBlank("blue").?;
    try testing.expect(b3.width == null);
    try testing.expectEqualStrings("blue", b3.color);

    try testing.expect(parseBlank("800") == null); // lone dimension
    try testing.expect(parseBlank("800 600 notacolour") == null);
    try testing.expect(parseBlank("red extra") == null);
}

test "parseBlank: leading page-format token, canonical + exclusive with dims" {
    const b1 = parseBlank("b5").?;
    try testing.expectEqualStrings("B5", b1.page.?);
    try testing.expect(b1.width == null);
    try testing.expectEqualStrings("white", b1.color);

    const b2 = parseBlank("A5 pink").?;
    try testing.expectEqualStrings("A5", b2.page.?);
    try testing.expectEqualStrings("pink", b2.color);

    const b3 = parseBlank("800 600 red").?; // dims still work without a format
    try testing.expect(b3.page == null);
    try testing.expectEqual(@as(u32, 800), b3.width.?);

    try testing.expect(parseBlank("b5 800 600") == null); // format + dims are exclusive
    try testing.expect(parseBlank("b5 notacolour") == null);
    try testing.expect(parseBlank("b5 red extra") == null);
}

test "parseConnectArgs: url/token pairs, multi-url form preserved" {
    const a = testing.allocator;

    // Plain single URL, no token.
    const p1 = try parseConnectArgs(a, "http://host:8090");
    defer a.free(p1);
    try testing.expectEqual(@as(usize, 1), p1.len);
    try testing.expectEqualStrings("http://host:8090", p1[0].url);
    try testing.expect(p1[0].token == null);

    // URL + token (base64url tokens have no dot/colon/scheme).
    const p2 = try parseConnectArgs(a, "http://host:8090 s3cr3t-tok_en");
    defer a.free(p2);
    try testing.expectEqual(@as(usize, 1), p2.len);
    try testing.expectEqualStrings("s3cr3t-tok_en", p2[0].token.?);

    // The old multi-URL form still parses as two connections.
    const p3 = try parseConnectArgs(a, "http://a:1 http://b:2");
    defer a.free(p3);
    try testing.expectEqual(@as(usize, 2), p3.len);
    try testing.expect(p3[0].token == null);
    try testing.expectEqualStrings("http://b:2", p3[1].url);

    // Mixed: a tokened URL followed by a bare one; localhost counts as a URL.
    const p4 = try parseConnectArgs(a, "host:8090 tok localhost");
    defer a.free(p4);
    try testing.expectEqual(@as(usize, 2), p4.len);
    try testing.expectEqualStrings("tok", p4[0].token.?);
    try testing.expectEqualStrings("localhost", p4[1].url);

    // A leading word is always the URL, even without a dot/port.
    const p5 = try parseConnectArgs(a, "myserver tok");
    defer a.free(p5);
    try testing.expectEqual(@as(usize, 1), p5.len);
    try testing.expectEqualStrings("myserver", p5[0].url);
    try testing.expectEqualStrings("tok", p5[0].token.?);
}

test "stripAlbum: removes the modifier and sets the flag" {
    const a = testing.allocator;
    var album = false;
    const s1 = try stripAlbum(a, "x1=0 x2=100px album", &album);
    defer a.free(s1);
    try testing.expect(album);
    try testing.expectEqualStrings("x1=0 x2=100px", s1);

    album = false;
    const s2 = try stripAlbum(a, "x1=0 x2=100px", &album);
    defer a.free(s2);
    try testing.expect(!album);
    try testing.expectEqualStrings("x1=0 x2=100px", s2);
}

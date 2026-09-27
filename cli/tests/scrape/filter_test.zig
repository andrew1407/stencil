//! The scrape filters: a media URL's format token, a `|`-separated token selection, and the
//! inclusive dimension bounds an unmeasured item passes.
const std = @import("std");
const filter = @import("../../src/scrape/filter.zig");
const dimensionPass = filter.dimensionPass;
const formatOf = filter.formatOf;
const tokenSelected = filter.tokenSelected;
const Sniff = @import("../../src/scrape/sniff.zig").Sniff;
const testing = std.testing;

test "formatOf: path, query, data, normalization" {
    var b: [16]u8 = undefined;
    try testing.expectEqualStrings("png", formatOf(&b, "http://x/a/logo.png"));
    try testing.expectEqualStrings("jpg", formatOf(&b, "http://x/p.JPEG?v=2"));
    try testing.expectEqualStrings("jpg", formatOf(&b, "http://x/p.jpg#frag"));
    try testing.expectEqualStrings("webp", formatOf(&b, "https://cdn.test/a.b.webp"));
    try testing.expectEqualStrings("svg", formatOf(&b, "data:image/svg+xml;base64,AAAA"));
    try testing.expectEqualStrings("png", formatOf(&b, "data:image/png;base64,AAAA"));
    try testing.expectEqualStrings("mov", formatOf(&b, "http://x/clip.MOV"));
    try testing.expectEqualStrings("mov", formatOf(&b, "data:video/quicktime,xx"));
    // norm is a SUBSTRING replacement (matches the extension's chained .replace): a data:
    // subtype like x-jpeg has its jpeg→jpg substring rewritten.
    try testing.expectEqualStrings("x-jpg", formatOf(&b, "data:image/x-jpeg;base64,AA"));
    try testing.expectEqualStrings("", formatOf(&b, "http://example.com")); // domain dot is not an ext
    try testing.expectEqualStrings("", formatOf(&b, "http://x/noext"));
    try testing.expectEqualStrings("", formatOf(&b, ""));
}

test "tokenSelected: all / subset / etc" {
    try testing.expect(tokenSelected("all", "img"));
    try testing.expect(tokenSelected("", "video"));
    try testing.expect(tokenSelected("png|jpg", "jpg"));
    try testing.expect(!tokenSelected("png|jpg", "webp"));
    try testing.expect(tokenSelected("img|video", "video"));
    try testing.expect(!tokenSelected("img", "background"));
}

test "dimensionPass: inclusive bounds, unknown passes" {
    const d = Sniff{ .width = 200, .height = 100, .fmt = "png" };
    try testing.expect(dimensionPass(d, 100, null, null, null));
    try testing.expect(dimensionPass(d, 200, 200, 100, 100)); // inclusive
    try testing.expect(!dimensionPass(d, 201, null, null, null)); // below min width
    try testing.expect(!dimensionPass(d, null, 199, null, null)); // above max width
    try testing.expect(!dimensionPass(d, null, null, null, 99)); // above max height
    try testing.expect(dimensionPass(null, 500, 600, 500, 600)); // unknown size passes
}

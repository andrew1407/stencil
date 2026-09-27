//! A fetched page scanned for media: scan order and kinds, dedupe, lazy and relative sources,
//! and a poster that tags the image it repeats.
const std = @import("std");
const parseMedia = @import("../../src/scrape/html.zig").parseMedia;
const testing = std.testing;

test "parseMedia: order, kinds, dedupe, lazy src, poster tag, background" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    const html =
        \\<html><head><base href="https://example.com/dir/"></head><body>
        \\<img src="logo.png" alt="Logo &amp; co">
        \\<img src="data:image/gif;base64,AAA" data-src="lazy.jpg">
        \\<svg><image xlink:href="/vec.svg"></image></svg>
        \\<picture><source src="hero.webp"><img src="hero.png"></picture>
        \\<video src="https://cdn.test/clip.mp4" poster="poster.png"></video>
        \\<div style="background-image:url('bg.jpg')"></div>
        \\<style>.x{background:url(https://example.com/dir/logo.png)}</style>
        \\</body></html>
    ;
    const items = try parseMedia(a, html, "https://example.com/page.html");
    // Scan order: imgs, svg image, video (+poster), picture source, backgrounds.
    // hero.png is a plain <img> inside <picture>; the picture <source> hero.webp is separate.
    try testing.expectEqualStrings("https://example.com/dir/logo.png", items[0].url);
    try testing.expectEqualStrings("Logo & co", items[0].alt);
    try testing.expect(items[0].kind == .img);
    try testing.expectEqualStrings("https://example.com/dir/lazy.jpg", items[1].url); // data: src → lazy fallback
    try testing.expectEqualStrings("https://example.com/dir/hero.png", items[2].url);
    try testing.expectEqualStrings("https://example.com/vec.svg", items[3].url);
    try testing.expect(items[3].kind == .img);
    try testing.expectEqualStrings("https://cdn.test/clip.mp4", items[4].url);
    try testing.expect(items[4].kind == .video);
    try testing.expectEqualStrings("https://example.com/dir/poster.png", items[5].url);
    try testing.expect(items[5].is_poster);
    try testing.expectEqualStrings("poster", items[5].category());
    try testing.expectEqualStrings("https://example.com/dir/hero.webp", items[6].url);
    try testing.expectEqualStrings("https://example.com/dir/bg.jpg", items[7].url);
    try testing.expect(items[7].kind == .bg);
    // logo.png in the <style> block dedupes against the first <img> (first wins).
    try testing.expectEqual(@as(usize, 8), items.len);
}

test "parseMedia: poster equal to an existing img just tags it" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    const html =
        \\<img src="https://x/same.png">
        \\<video src="https://x/v.mp4" poster="https://x/same.png"></video>
    ;
    const items = try parseMedia(a, html, "https://x/p");
    try testing.expectEqual(@as(usize, 2), items.len); // img + video only; poster tags the img
    try testing.expectEqualStrings("https://x/same.png", items[0].url);
    try testing.expect(items[0].is_poster);
    try testing.expect(items[1].kind == .video);
}

test "parseMedia: relative video src / source src resolve then pass the http(s) gate" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    // A relative <video src> must be resolved against the base FIRST, then scheme-checked, so
    // it IS emitted as a resolved absolute video item (parity with pystencil / the extension).
    {
        const html = "<video src=\"clip.mp4\"></video>";
        const items = try parseMedia(a, html, "https://example.com/dir/page.html");
        try testing.expectEqual(@as(usize, 1), items.len);
        try testing.expectEqualStrings("https://example.com/dir/clip.mp4", items[0].url);
        try testing.expect(items[0].kind == .video);
    }
    // Same for a relative in-<video> <source src> when the <video src> is absent.
    {
        const html = "<video><source src=\"movie.webm\"></video>";
        const items = try parseMedia(a, html, "https://example.com/dir/page.html");
        try testing.expectEqual(@as(usize, 1), items.len);
        try testing.expectEqualStrings("https://example.com/dir/movie.webm", items[0].url);
        try testing.expect(items[0].kind == .video);
    }
}

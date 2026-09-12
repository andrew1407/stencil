//! Extracting media out of a fetched page. The HTML is UNTRUSTED content: it is scanned
//! with a byte walker (no DOM, no eval), and every URL it yields is still filtered and
//! guarded before anything is fetched.
const std = @import("std");
const net = @import("../net.zig");
const image = @import("../image.zig");
const testing = std.testing;
const urls = @import("urls.zig");
const text = @import("text.zig");
const extractCssUrls = text.extractCssUrls;
const filter = @import("filter.zig");
const Kind = filter.Kind;
const Media = filter.Media;
const decodeEntities = text.decodeEntities;
const eqlCI = text.eqlCI;
const getAttr = text.getAttr;
const indexOfPosCI = text.indexOfPosCI;
const isSpace = text.isSpace;
const resolveUrl = urls.resolveUrl;
const resolvesHttp = urls.resolvesHttp;

const RawImg = struct { url: []const u8, alt: []const u8 };
const RawVideo = struct { url: []const u8, poster: []const u8, alt: []const u8 };

/// Parse `html`, extracting media URLs resolved absolute against `base_url` (honoring a
/// `<base href>`), deduped first-wins, in the scan order: <img>, <svg><image>, <video>
/// (+ poster), <picture><source>, then CSS `url(...)` backgrounds. Owned by `alloc`; http(s) only.
pub fn parseMedia(alloc: std.mem.Allocator, html: []const u8, base_url: []const u8) ![]Media {
    var imgs: std.ArrayList(RawImg) = .empty;
    defer imgs.deinit(alloc);
    var svgs: std.ArrayList(RawImg) = .empty;
    defer svgs.deinit(alloc);
    var videos: std.ArrayList(RawVideo) = .empty;
    defer videos.deinit(alloc);
    var pics: std.ArrayList([]const u8) = .empty;
    defer pics.deinit(alloc);
    var bgs: std.ArrayList([]const u8) = .empty;
    defer bgs.deinit(alloc);

    var base: []const u8 = base_url;
    var base_set = false;
    var in_video = false;
    var vid_idx: usize = 0;
    var in_picture = false;

    var i: usize = 0;
    while (i < html.len) {
        if (html[i] != '<') {
            i += 1;
            continue;
        }
        // Comments / doctype / processing instructions.
        if (std.mem.startsWith(u8, html[i..], "<!--")) {
            const end = std.mem.indexOfPos(u8, html, i + 4, "-->") orelse html.len;
            i = @min(end + 3, html.len);
            continue;
        }
        if (i + 1 < html.len and (html[i + 1] == '!' or html[i + 1] == '?')) {
            i = (std.mem.indexOfScalarPos(u8, html, i, '>') orelse (html.len - 1)) + 1;
            continue;
        }
        // Read to the matching '>' (respecting quoted attribute values).
        var j = i + 1;
        var quote: u8 = 0;
        while (j < html.len) : (j += 1) {
            const c = html[j];
            if (quote != 0) {
                if (c == quote) quote = 0;
            } else if (c == '"' or c == '\'') {
                quote = c;
            } else if (c == '>') break;
        }
        const tag = html[i + 1 .. @min(j, html.len)];
        i = @min(j + 1, html.len);
        if (tag.len == 0) continue;

        const is_close = tag[0] == '/';
        const name_start: usize = if (is_close) 1 else 0;
        var k = name_start;
        while (k < tag.len and !isSpace(tag[k]) and tag[k] != '/') : (k += 1) {}
        const name = tag[name_start..k];
        const body = tag[k..];

        if (is_close) {
            if (eqlCI(name, "video")) in_video = false;
            if (eqlCI(name, "picture")) in_picture = false;
            continue;
        }

        // <style>/<script>: skip the raw content (don't parse tags inside); harvest CSS
        // url(...) from a <style> block. Void/self-closed forms have no content.
        if (eqlCI(name, "style") or eqlCI(name, "script")) {
            const close = if (eqlCI(name, "style")) "</style" else "</script";
            const content_end = indexOfPosCI(html, i, close) orelse html.len;
            if (eqlCI(name, "style")) try extractCssUrls(alloc, &bgs, html[i..content_end]);
            i = if (content_end < html.len)
                (std.mem.indexOfScalarPos(u8, html, content_end, '>') orelse (html.len - 1)) + 1
            else
                html.len;
            continue;
        }

        // Any element may carry an inline background via style="".
        if (getAttr(body, "style")) |st| try extractCssUrls(alloc, &bgs, st);

        if (eqlCI(name, "base")) {
            if (!base_set) {
                if (getAttr(body, "href")) |href| {
                    if (try resolveUrl(alloc, base_url, href)) |abs| {
                        base = abs;
                        base_set = true;
                    }
                }
            }
        } else if (eqlCI(name, "img")) {
            try imgs.append(alloc, .{ .url = pickImgUrl(body), .alt = getAttr(body, "alt") orelse "" });
        } else if (eqlCI(name, "image")) {
            try svgs.append(alloc, .{ .url = getAttr(body, "href") orelse getAttr(body, "xlink:href") orelse "", .alt = "" });
        } else if (eqlCI(name, "video")) {
            const src = getAttr(body, "src") orelse "";
            vid_idx = videos.items.len;
            in_video = true;
            try videos.append(alloc, .{
                .url = if (try resolvesHttp(alloc, base, src)) src else "",
                .poster = getAttr(body, "poster") orelse "",
                .alt = getAttr(body, "aria-label") orelse "",
            });
        } else if (eqlCI(name, "picture")) {
            in_picture = true;
        } else if (eqlCI(name, "source")) {
            const src = getAttr(body, "src") orelse "";
            if (in_video) {
                if (videos.items[vid_idx].url.len == 0 and try resolvesHttp(alloc, base, src)) videos.items[vid_idx].url = src;
            } else if (in_picture and src.len != 0) {
                try pics.append(alloc, src);
            }
        }
    }

    // Resolution + dedupe phase, in category order.
    var out: std.ArrayList(Media) = .empty;
    errdefer out.deinit(alloc);
    var seen = std.StringHashMap(usize).init(alloc);
    defer seen.deinit();

    for (imgs.items) |r| try addMedia(alloc, &out, &seen, base, r.url, .img, r.alt, false);
    for (svgs.items) |r| try addMedia(alloc, &out, &seen, base, r.url, .img, "", false);
    for (videos.items) |v| {
        if (v.url.len != 0) try addMedia(alloc, &out, &seen, base, v.url, .video, v.alt, false);
        if (v.poster.len != 0) try addMedia(alloc, &out, &seen, base, v.poster, .img, "", true);
    }
    for (pics.items) |r| try addMedia(alloc, &out, &seen, base, r, .img, "", false);
    for (bgs.items) |r| try addMedia(alloc, &out, &seen, base, r, .bg, "", false);

    return out.toOwnedSlice(alloc);
}

/// Resolve `raw` against `base`, keep only http(s), decode HTML entities in the alt text,
/// and append — deduping on the absolute URL (first wins). A duplicate that arrives tagged
/// `is_poster` just promotes the already-collected item to the poster category.
fn addMedia(
    alloc: std.mem.Allocator,
    out: *std.ArrayList(Media),
    seen: *std.StringHashMap(usize),
    base: []const u8,
    raw: []const u8,
    kind: Kind,
    alt: []const u8,
    is_poster: bool,
) !void {
    const abs = (try resolveUrl(alloc, base, raw)) orelse return;
    if (!net.isUrl(abs)) return; // drop data:/blob:/other schemes for download
    if (seen.get(abs)) |idx| {
        if (is_poster) out.items[idx].is_poster = true;
        return;
    }
    const alt_dec = if (alt.len == 0) "" else try decodeEntities(alloc, alt);
    try out.append(alloc, .{ .url = abs, .kind = kind, .alt = alt_dec, .is_poster = is_poster });
    try seen.put(abs, out.items.len - 1);
}

/// The <img> URL: `src` unless it is empty or a `data:` placeholder, in which case fall back
/// to the first non-empty lazy attribute / first `srcset` candidate (static-HTML adaptation).
fn pickImgUrl(body: []const u8) []const u8 {
    const src = getAttr(body, "src") orelse "";
    if (src.len != 0 and !std.ascii.startsWithIgnoreCase(src, "data:")) return src;
    const lazies = [_][]const u8{ "data-src", "data-original", "data-lazy-src" };
    for (lazies) |a| {
        if (getAttr(body, a)) |v| if (v.len != 0) return v;
    }
    if (getAttr(body, "srcset")) |ss| {
        const first = firstSrcset(ss);
        if (first.len != 0) return first;
    }
    return src;
}

/// First URL of a `srcset` (the token before the first whitespace / comma).
fn firstSrcset(ss: []const u8) []const u8 {
    const s = std.mem.trim(u8, ss, " \t\r\n");
    const end = std.mem.indexOfAny(u8, s, " \t\r\n,") orelse s.len;
    return s[0..end];
}

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

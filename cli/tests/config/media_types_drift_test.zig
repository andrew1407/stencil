//! Drift guard for the media-type canon: src/media/types.zig now feeds video.zig's
//! looksLikeVideo and scrape.zig's format normalisation + extension allow-list from
//! browser/js/config/mediaTypes.json. These pin the values the CLI used before the move —
//! so the asset cannot silently change what the CLI accepts — and hold the asset's own
//! internal claims (the cli surface list is a subset of the contract set).
const std = @import("std");
const mediaTypes = @import("../../src/media/types.zig");
const video = @import("../../src/media/video.zig");
const scrape = @import("../../src/scrape.zig");
const testing = std.testing;

const media_types_json = @embedFile("mediaTypes.json");

// What src/video.zig held as `video_exts` before it read the canon. Byte-identical, in order.
const historic_video_exts = [_][]const u8{
    ".mp4", ".mov", ".mkv", ".webm", ".avi", ".m4v", ".mpg", ".mpeg", ".wmv", ".flv", ".ts", ".gifv",
};

test "the cli's video extensions are the asset's, unchanged from the inline list" {
    const got = mediaTypes.videoExts();
    try testing.expectEqual(historic_video_exts.len, got.len);
    for (historic_video_exts, got) |want, have| try testing.expectEqualStrings(want, have);
}

test "the cli video list is surfaces.cli.video, and a subset of the contract set" {
    const a = testing.allocator;
    const parsed = try std.json.parseFromSlice(std.json.Value, a, media_types_json, .{});
    defer parsed.deinit();
    const root = parsed.value.object;
    const cli = root.get("surfaces").?.object.get("cli").?.object.get("video").?.array;
    const contract = root.get("video").?.object.get("extensions").?.array;

    try testing.expectEqual(cli.items.len, mediaTypes.videoExts().len);
    for (cli.items, mediaTypes.videoExts()) |v, dotted| {
        try testing.expectEqualStrings(v.string, dotted[1..]); // the CLI adds the dot
        var known = false;
        for (contract.items) |c| known = known or std.mem.eql(u8, c.string, v.string);
        if (!known) {
            std.debug.print("cli recognises '{s}', which video.extensions does not list\n", .{v.string});
            return error.UnknownVideoExtension;
        }
    }
}

test "looksLikeVideo still answers exactly as the inline list did" {
    try testing.expect(video.looksLikeVideo("clip.MP4"));
    try testing.expect(video.looksLikeVideo("https://h/v.webm?token=1"));
    try testing.expect(video.looksLikeVideo("reel.gifv")); // cli-only in the canon
    try testing.expect(!video.looksLikeVideo("photo.png"));
    // In the contract set, but never in the CLI's — wiring the canon must not widen it.
    try testing.expect(!video.looksLikeVideo("clip.3gp"));
    try testing.expect(!video.looksLikeVideo("clip.ogv"));
    try testing.expect(!video.looksLikeVideo("clip.ogg"));
}

test "format normalisation keeps the asset's three rewrites, in order" {
    const want = [_][2][]const u8{
        .{ "jpeg", "jpg" }, .{ "svg+xml", "svg" }, .{ "quicktime", "mov" },
    };
    const got = mediaTypes.normalizations();
    try testing.expectEqual(want.len, got.len);
    for (want, got) |w, g| {
        try testing.expectEqualStrings(w[0], g[0]);
        try testing.expectEqualStrings(w[1], g[1]);
    }
    var buf: [16]u8 = undefined;
    try testing.expectEqualStrings("jpg", scrape.formatOf(&buf, "https://h/a.JPEG"));
    try testing.expectEqualStrings("svg", scrape.formatOf(&buf, "data:image/svg+xml;base64,AA"));
    try testing.expectEqualStrings("mov", scrape.formatOf(&buf, "data:video/quicktime,AA"));
    try testing.expectEqualStrings("x-jpg", scrape.formatOf(&buf, "data:image/x-jpeg,AA"));
}

test "the extension allow-list is the asset's pattern, still 2-5 characters" {
    const a = testing.allocator;
    const parsed = try std.json.parseFromSlice(std.json.Value, a, media_types_json, .{});
    defer parsed.deinit();
    try testing.expectEqualStrings("^[a-z0-9]{2,5}$", parsed.value.object.get("extensionPattern").?.string);

    try testing.expect(!mediaTypes.extLenOk(1));
    try testing.expect(mediaTypes.extLenOk(2) and mediaTypes.extLenOk(5));
    try testing.expect(!mediaTypes.extLenOk(6));
    var buf: [16]u8 = undefined;
    try testing.expectEqualStrings("", scrape.formatOf(&buf, "https://h/a.j")); // 1 char
    try testing.expectEqualStrings("png", scrape.formatOf(&buf, "https://h/a.png"));
    try testing.expectEqualStrings("", scrape.formatOf(&buf, "https://h/a.toolong")); // 7 chars
}

test "the data: prefixes come from the asset" {
    try testing.expectEqualStrings("image/", mediaTypes.imagePrefix());
    try testing.expectEqualStrings("video/", mediaTypes.videoPrefix());
}

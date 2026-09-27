//! run() orchestration tests: filter → window → create dir → write files → the §3 stderr grammar. A
//! `Deps` seam swaps net.fetch/report.print/cwd for in-memory mocks, so this needs no server or disk.
const std = @import("std");
const args = @import("../../src/args.zig");
const runImpl = @import("../../src/scrape/run.zig").runImpl;
const run_mock = @import("run_mock.zig");
const MockIo = run_mock.MockIo;
const mkPng = run_mock.mkPng;
const testing = std.testing;

test "run: category+format filter, wrote grammar, dir, summary" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var mock = MockIo.init(a);
    // Mixed-case host to exercise the output-line lowercasing; c.jpg is an img but not png.
    const html = "<img src=\"a.png\"><img src=\"b.png\"><img src=\"c.jpg\">" ++
        "<video src=\"http://cdn.test/clip.mp4\" poster=\"p.png\"></video>";
    try mock.serve("http://Example.com/", html);
    try mock.serve("http://Example.com/a.png", mkPng(a, 10, 20));
    try mock.serve("http://Example.com/b.png", mkPng(a, 30, 40));

    var opts = args.Options{};
    opts.source_site = "http://Example.com/";
    opts.output = "out";
    opts.source_filter = "img";
    opts.source_format = "png";
    try runImpl(testing.allocator, io, opts, mock.deps());

    try testing.expectEqual(@as(usize, 2), mock.files.count());
    try testing.expectEqualSlices(u8, mkPng(a, 10, 20), mock.files.get("out/a.png").?);
    try testing.expectEqualSlices(u8, mkPng(a, 30, 40), mock.files.get("out/b.png").?);
    try testing.expectEqual(@as(usize, 1), mock.dirs.items.len);
    try testing.expectEqualStrings("out", mock.dirs.items[0]);
    try testing.expectEqual(@as(usize, 4), mock.lines.items.len);
    try testing.expectEqualStrings("scraping http://Example.com/…\n", mock.line(0));
    try testing.expectEqualStrings("wrote out/a.png (10x20 px · source example.com)\n", mock.line(1));
    try testing.expectEqualStrings("wrote out/b.png (30x40 px · source example.com)\n", mock.line(2));
    try testing.expectEqualStrings("scraped 2 file(s) from example.com into out\n", mock.line(3));
}

test "run: all categories, count 0 = all, unmeasured video line" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var mock = MockIo.init(a);
    const html = "<img src=\"a.png\"><video src=\"http://cdn.test/clip.mp4\" poster=\"p.png\"></video>";
    try mock.serve("http://example.com/", html);
    try mock.serve("http://example.com/a.png", mkPng(a, 10, 20));
    try mock.serve("http://cdn.test/clip.mp4", "not-an-image mp4 bytes");
    try mock.serve("http://example.com/p.png", mkPng(a, 5, 5));

    var opts = args.Options{};
    opts.source_site = "http://example.com/";
    opts.output = "media";
    opts.source_count = 0; // 0 = all
    try runImpl(testing.allocator, io, opts, mock.deps());

    // img, then video-before-poster; the video is unmeasured → the no-dims `(source …)` line.
    try testing.expectEqual(@as(usize, 3), mock.files.count());
    try testing.expectEqual(@as(usize, 5), mock.lines.items.len);
    try testing.expectEqualStrings("scraping http://example.com/…\n", mock.line(0));
    try testing.expectEqualStrings("wrote media/a.png (10x20 px · source example.com)\n", mock.line(1));
    try testing.expectEqualStrings("wrote media/clip.mp4 (source example.com)\n", mock.line(2));
    try testing.expectEqualStrings("wrote media/p.png (5x5 px · source example.com)\n", mock.line(3));
    try testing.expectEqualStrings("scraped 3 file(s) from example.com into media\n", mock.line(4));
}

test "run: group/count windows the filtered list, fetching only the window" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var mock = MockIo.init(a);
    try mock.serve("http://example.com/", "<img src=\"a.png\"><img src=\"b.png\"><img src=\"c.png\">");
    try mock.serve("http://example.com/a.png", mkPng(a, 1, 1));
    try mock.serve("http://example.com/b.png", mkPng(a, 2, 2));
    try mock.serve("http://example.com/c.png", mkPng(a, 3, 3));

    var opts = args.Options{};
    opts.source_site = "http://example.com/";
    opts.output = "out";
    opts.source_count = 1;
    opts.group = 1; // window = filtered[1..2] = [b]
    try runImpl(testing.allocator, io, opts, mock.deps());

    try testing.expectEqual(@as(usize, 1), mock.files.count());
    try testing.expect(mock.files.get("out/b.png") != null);
    try testing.expect(mock.files.get("out/a.png") == null); // outside the window → never fetched
    try testing.expectEqualStrings("scraping http://example.com/…\n", mock.line(0));
    try testing.expectEqualStrings("wrote out/b.png (2x2 px · source example.com)\n", mock.line(1));
    try testing.expectEqualStrings("scraped 1 file(s) from example.com into out\n", mock.line(2));
}

test "run: no matching media is a hard error" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var mock = MockIo.init(a);
    try mock.serve("http://example.com/", "<img src=\"a.png\">");
    try mock.serve("http://example.com/a.png", mkPng(a, 10, 20));

    var opts = args.Options{};
    opts.source_site = "http://example.com/";
    opts.output = "out";
    opts.source_filter = "video"; // page has no video → nothing matches
    try testing.expectError(error.NoMediaMatched, runImpl(testing.allocator, io, opts, mock.deps()));

    try testing.expectEqual(@as(usize, 0), mock.files.count());
    try testing.expectEqual(@as(usize, 2), mock.lines.items.len);
    try testing.expectEqualStrings("scraping http://example.com/…\n", mock.line(0));
    try testing.expectEqualStrings("error: no media matched at http://example.com/\n", mock.line(1));
}

test "run: --confine-output keeps the destination dir inside the cwd; default allows it" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var mock = MockIo.init(a);
    try mock.serve("http://example.com/", "<img src=\"cat.png\">");
    try mock.serve("http://example.com/cat.png", mkPng(a, 1, 1));

    var opts = args.Options{};
    opts.source_site = "http://example.com/";
    opts.output = "/tmp/stencil_scrape";
    opts.confine_output = true;
    try testing.expectError(error.UnsafeOutputPath, runImpl(testing.allocator, io, opts, mock.deps()));
    opts.output = "~/stencil_scrape";
    try testing.expectError(error.UnsafeOutputPath, runImpl(testing.allocator, io, opts, mock.deps()));
    try testing.expectEqual(@as(usize, 0), mock.files.count());

    // A relative dir is fine confined, and an absolute one is fine without the flag.
    opts.output = "out";
    try runImpl(testing.allocator, io, opts, mock.deps());
    opts.output = "/tmp/stencil_scrape";
    opts.confine_output = false;
    try runImpl(testing.allocator, io, opts, mock.deps());
    try testing.expect(mock.files.get("out/cat.png") != null);
    try testing.expect(mock.files.get("/tmp/stencil_scrape/cat.png") != null);
}

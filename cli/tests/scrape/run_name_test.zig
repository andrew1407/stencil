//! run() under `--source-name`: candidates filtered by URL, as a regex where the platform has
//! one, and a pattern refused before it compiles when it is too long or malformed.
const std = @import("std");
const args = @import("../../src/args.zig");
const filters = @import("../../src/scrape/filter.zig");
const NameMatcher = filters.NameMatcher;
const has_posix_regex = filters.has_posix_regex;
const max_name_pattern = filters.max_name_pattern;
const runImpl = @import("../../src/scrape/run.zig").runImpl;
const run_mock = @import("run_mock.zig");
const MockIo = run_mock.MockIo;
const mkPng = run_mock.mkPng;
const testing = std.testing;

test "run: --source-name filters candidates by URL (regex or substring)" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var mock = MockIo.init(a);
    try mock.serve("http://example.com/", "<img src=\"cat.png\"><img src=\"dog.jpg\"><img src=\"cat2.png\">");
    try mock.serve("http://example.com/cat.png", mkPng(a, 1, 1));
    try mock.serve("http://example.com/cat2.png", mkPng(a, 2, 2));
    try mock.serve("http://example.com/dog.jpg", mkPng(a, 3, 3));

    var opts = args.Options{};
    opts.source_site = "http://example.com/";
    opts.output = "out";
    opts.source_count = 0; // all
    opts.source_name = "cat"; // both a valid regex and substring → keeps cat.png + cat2.png
    try runImpl(testing.allocator, io, opts, mock.deps());

    try testing.expectEqual(@as(usize, 2), mock.files.count());
    try testing.expect(mock.files.get("out/cat.png") != null);
    try testing.expect(mock.files.get("out/cat2.png") != null);
    try testing.expect(mock.files.get("out/dog.jpg") == null); // filtered out by name
}

test "run: --source-name honours regex metacharacters (POSIX)" {
    if (!has_posix_regex) return error.SkipZigTest;
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var mock = MockIo.init(a);
    try mock.serve("http://example.com/", "<img src=\"cat.png\"><img src=\"dog.jpg\">");
    try mock.serve("http://example.com/cat.png", mkPng(a, 1, 1));
    try mock.serve("http://example.com/dog.jpg", mkPng(a, 3, 3));

    var opts = args.Options{};
    opts.source_site = "http://example.com/";
    opts.output = "out";
    opts.source_count = 0;
    opts.source_name = "\\.jpg$"; // anchored regex → only the .jpg
    try runImpl(testing.allocator, io, opts, mock.deps());

    try testing.expectEqual(@as(usize, 1), mock.files.count());
    try testing.expect(mock.files.get("out/dog.jpg") != null);
    try testing.expect(mock.files.get("out/cat.png") == null);
}

test "run: an over-long --source-name pattern is refused before regcomp" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var mock = MockIo.init(a);
    try mock.serve("http://example.com/", "<img src=\"cat.png\">");

    // A nest of counted repetitions is exactly the shape glibc's regexec backtracks on.
    const long = "(a+)+" ** 60;
    try testing.expect(long.len > max_name_pattern);
    var opts = args.Options{};
    opts.source_site = "http://example.com/";
    opts.output = "out";
    opts.source_name = long;
    try testing.expectError(error.NamePatternTooLong, runImpl(testing.allocator, io, opts, mock.deps()));
    try testing.expectEqual(@as(usize, 0), mock.files.count());
    // A pattern at the cap still compiles.
    opts.source_name = "c" ++ ("a" ** (max_name_pattern - 1));
    try testing.expect(NameMatcher.init(opts.source_name, a) catch null != null);
}

test "run: --source-name invalid regex is a hard error (POSIX)" {
    if (!has_posix_regex) return error.SkipZigTest;
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var mock = MockIo.init(a);
    try mock.serve("http://example.com/", "<img src=\"cat.png\">");

    var opts = args.Options{};
    opts.source_site = "http://example.com/";
    opts.output = "out";
    opts.source_name = "cat("; // unbalanced paren → regcomp fails
    try testing.expectError(error.BadNamePattern, runImpl(testing.allocator, io, opts, mock.deps()));
    try testing.expectEqual(@as(usize, 0), mock.files.count());
}

//! Scrape mode end to end: fetch the page, extract, filter, window, download and write.
//! Everything it touches outside the pure helpers goes through the injected `Deps`, which is
//! why the whole loop is tested offline with no network and no disk.
const std = @import("std");
const seam = @import("deps.zig");
const Deps = seam.Deps;
const realFetch = seam.realFetch;
const realEmit = seam.realEmit;
const realMkdir = seam.realMkdir;
const realWrite = seam.realWrite;
const net = @import("../net.zig");
const image = @import("../image.zig");
const pipeline = @import("../pipeline.zig");
const args = @import("../args.zig");
const confine = @import("../confine.zig");
const report = @import("../report.zig");
const fetchPool = @import("../fetchPool.zig");
const testing = std.testing;
const windowing = @import("window.zig");
const urls = @import("urls.zig");
const sniffer = @import("sniff.zig");
const page = @import("html.zig");
const filters = @import("filter.zig");
const Media = filters.Media;
const NameMatcher = filters.NameMatcher;
const categoryPass = filters.categoryPass;
const dimensionPass = filters.dimensionPass;
const formatPass = filters.formatPass;
const has_posix_regex = filters.has_posix_regex;
const max_name_pattern = filters.max_name_pattern;
const parseMedia = page.parseMedia;
const Sniff = sniffer.Sniff;
const png_sig = sniffer.png_sig;
const sniff = sniffer.sniff;
const deriveName = urls.deriveName;
const joinPath = urls.joinPath;
const boundOpt = windowing.boundOpt;
const effectiveCount = windowing.effectiveCount;
const formatFor = windowing.formatFor;
const subStrict = windowing.subStrict;
const window = windowing.window;

const MAX_HTML = 32 << 20; // sanity cap on a scraped page (fetch itself is unbounded)

/// Scrape `opts.source_site`, filter, download the matching window into `opts.output`, and print the §3
/// stderr lines. Per-item fetch failures are non-fatal; zero files written is a hard error (exit 1).
pub fn run(gpa: std.mem.Allocator, io: std.Io, opts: args.Options) !void {
    var unused: u8 = 0;
    const deps = Deps{
        .ctx = @ptrCast(&unused),
        .fetchFn = realFetch,
        .emitFn = realEmit,
        .mkdirFn = realMkdir,
        .writeFn = realWrite,
    };
    return runImpl(gpa, io, opts, deps);
}

fn runImpl(gpa: std.mem.Allocator, io: std.Io, opts: args.Options, deps: Deps) !void {
    var arena_state = std.heap.ArenaAllocator.init(gpa);
    defer arena_state.deinit();
    const arena = arena_state.allocator();

    const site = opts.source_site.?;
    const host_raw = net.hostOf(site) orelse {
        deps.err(arena, "could not parse a host from URL '{s}'\n", .{site});
        return error.BadSourceUrl;
    };
    const dir = opts.output orelse ".";
    if (pipeline.hasParentTraversal(dir) or (opts.confine_output and confine.outsideCwd(dir))) {
        deps.err(arena, "refusing to write to a path that escapes the working directory: '{s}'\n", .{dir});
        return error.UnsafeOutputPath;
    }

    // Lowercase the host LOCALLY for the output lines only (parity with pystencil's
    // urlparse().hostname, which is already lowercase); net.hostOf stays case-preserving.
    const host = try std.ascii.allocLowerString(arena, host_raw);

    // Compile the optional --source-name regex before any I/O so a bad pattern fails fast.
    var name_matcher = NameMatcher.init(opts.source_name, arena) catch |e| {
        if (e == error.NamePatternTooLong) {
            deps.err(arena, "--source-name pattern is too long (max {d} characters)\n", .{max_name_pattern});
        } else deps.err(arena, "invalid --source-name regex '{s}'\n", .{opts.source_name.?});
        return e;
    };
    defer name_matcher.deinit();

    // Announce the scrape up front: the page fetch and the downloads take a while. It carries none of the
    // parsed prefixes (`wrote `/`scraped `/`error:`), so the mcp and bot adapters ignore it.
    deps.emit(arena, "scraping {s}…\n", .{site});

    const html = try deps.fetch(arena, io, site, false); // net prints its own error on failure
    if (html.len > MAX_HTML) {
        deps.err(arena, "scraped page too large ({d} bytes)\n", .{html.len});
        return error.PageTooLarge;
    }

    const medias = try parseMedia(arena, html, site);

    const filter = opts.source_filter orelse "all";
    const formats = opts.source_format orelse "all";
    const min_w = boundOpt(opts.source_min_width);
    const max_w = boundOpt(opts.source_max_width);
    const min_h = boundOpt(opts.source_min_height);
    const max_h = boundOpt(opts.source_max_height);
    const dim_active = min_w != null or max_w != null or min_h != null or max_h != null;

    // Category + format filter (cheap, no network).
    var candidates: std.ArrayList(Media) = .empty;
    defer candidates.deinit(arena);
    for (medias) |m| {
        if (categoryPass(m, filter) and formatPass(m, formats) and name_matcher.matches(m.url, arena))
            try candidates.append(arena, m);
    }

    // A candidate ready to write. `bytes == null` means measurement could not fetch it: it still occupies
    // its window slot (unknown size passes the filter, as in pystencil) and the write loop re-fetches it.
    const Ready = struct { media: Media, bytes: ?[]const u8, dims: ?Sniff };
    var ready: std.ArrayList(Ready) = .empty;
    defer ready.deinit(arena);

    // A dimension filter must measure every candidate before the window can be cut; without one only the
    // window is fetched. These URLs came from page content, so loopback is allowed only on the named host.
    const count = effectiveCount(opts.source_count);
    const to_fetch = if (dim_active) candidates.items else window(Media, candidates.items, opts.group, count);
    const jobs = try arena.alloc(fetchPool.Job, to_fetch.len);
    for (to_fetch, jobs) |m, *j| j.* = .{ .url = m.url, .strict = subStrict(m.url, host) };
    var batch = try fetchPool.fetchAll(gpa, io, deps.ctx, deps.fetchFn, jobs);
    defer batch.deinit();

    var passed: std.ArrayList(Ready) = .empty; // dimension-passers, before the window is cut
    defer passed.deinit(arena);
    for (to_fetch, batch.results) |m, got| {
        const bytes = got.bytes orelse {
            // A measurement failure is silent (parity with pystencil's best-effort _measure_item): the item keeps
            // its window slot as unknown-size and the write loop re-fetches. Unmeasured, there is no second pass.
            if (dim_active) try passed.append(arena, .{ .media = m, .bytes = null, .dims = null }) //
            else deps.err(arena, "could not fetch {s} ({s})\n", .{ m.url, @errorName(got.err.?) });
            continue;
        };
        const dims = sniff(bytes);
        if (!dim_active) try ready.append(arena, .{ .media = m, .bytes = bytes, .dims = dims }) //
        else if (dimensionPass(dims, min_w, max_w, min_h, max_h))
            try passed.append(arena, .{ .media = m, .bytes = bytes, .dims = dims });
    }
    if (dim_active) for (window(Ready, passed.items, opts.group, count)) |it| try ready.append(arena, it);

    if (ready.items.len != 0 and !std.mem.eql(u8, dir, ".")) {
        deps.mkdir(io, dir) catch |e| {
            deps.err(arena, "could not create output directory '{s}' ({s})\n", .{ dir, @errorName(e) });
            return e;
        };
    }

    var used = std.StringHashMap(void).init(gpa);
    defer used.deinit();
    var written: usize = 0;
    for (ready.items, 0..) |it, idx| {
        // Re-fetch items whose measurement fetch failed (bytes == null); a second failure is
        // the non-fatal per-item error, matching pystencil's download pass.
        var dims = it.dims;
        const bytes = it.bytes orelse blk: {
            const b = deps.fetch(arena, io, it.media.url, subStrict(it.media.url, host)) catch |e| {
                deps.err(arena, "could not fetch {s} ({s})\n", .{ it.media.url, @errorName(e) });
                continue;
            };
            dims = sniff(b);
            break :blk b;
        };
        var fbuf: [16]u8 = undefined;
        const ext = formatFor(&fbuf, it.media, dims);
        var name = try deriveName(arena, it.media.url, ext, idx);
        if (used.contains(name)) name = try std.fmt.allocPrint(arena, "source-{d}.{s}", .{ idx, ext });
        try used.put(name, {});
        if (pipeline.hasParentTraversal(name)) continue; // sanitized names never do; belt-and-braces
        const path = try joinPath(arena, dir, name);
        deps.write(io, path, bytes) catch |e| {
            deps.err(arena, "could not write {s} ({s})\n", .{ path, @errorName(e) });
            continue;
        };
        if (dims) |d| {
            deps.emit(arena, "wrote {s} ({d}x{d} px · source {s})\n", .{ path, d.width, d.height, host });
        } else {
            deps.emit(arena, "wrote {s} (source {s})\n", .{ path, host });
        }
        written += 1;
    }

    if (written == 0) {
        deps.err(arena, "no media matched at {s}\n", .{site});
        return error.NoMediaMatched;
    }
    deps.emit(arena, "scraped {d} file(s) from {s} into {s}\n", .{ written, host, dir });
}

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


// run() orchestration tests: filter → window → create dir → write files → the §3 stderr grammar. A
// `Deps` seam swaps net.fetch/report.print/cwd for in-memory mocks, so this needs no server or disk.

/// A 24-byte PNG header (signature + IHDR) carrying `w`×`h` (both < 256) so `sniff` measures it.
fn mkPng(a: std.mem.Allocator, w: u8, h: u8) []const u8 {
    const b = a.alloc(u8, 24) catch unreachable;
    @memcpy(b[0..8], &png_sig);
    @memcpy(b[8..12], &[_]u8{ 0, 0, 0, 0x0d }); // IHDR length 13
    @memcpy(b[12..16], "IHDR");
    @memcpy(b[16..20], &[_]u8{ 0, 0, 0, w }); // width, big-endian
    @memcpy(b[20..24], &[_]u8{ 0, 0, 0, h }); // height, big-endian
    return b;
}

const MockIo = struct {
    a: std.mem.Allocator,
    fetches: std.StringHashMap([]const u8),
    files: std.StringHashMap([]const u8),
    lines: std.ArrayListUnmanaged([]const u8) = .empty,
    dirs: std.ArrayListUnmanaged([]const u8) = .empty,

    fn init(a: std.mem.Allocator) MockIo {
        return .{
            .a = a,
            .fetches = std.StringHashMap([]const u8).init(a),
            .files = std.StringHashMap([]const u8).init(a),
        };
    }
    fn serve(self: *MockIo, url: []const u8, bytes: []const u8) !void {
        try self.fetches.put(url, bytes);
    }
    fn deps(self: *MockIo) Deps {
        return .{ .ctx = @ptrCast(self), .fetchFn = fetchFn, .emitFn = emitFn, .mkdirFn = mkdirFn, .writeFn = writeFn };
    }
    fn line(self: *MockIo, i: usize) []const u8 {
        return self.lines.items[i];
    }
    fn fetchFn(ptr: *anyopaque, a: std.mem.Allocator, _: std.Io, url: []const u8, _: bool) anyerror![]u8 {
        const self: *MockIo = @ptrCast(@alignCast(ptr));
        const v = self.fetches.get(url) orelse return error.HttpFailed;
        return a.dupe(u8, v);
    }
    fn emitFn(ptr: *anyopaque, s: []const u8) void {
        const self: *MockIo = @ptrCast(@alignCast(ptr));
        const dup = self.a.dupe(u8, s) catch return;
        self.lines.append(self.a, dup) catch {};
    }
    fn mkdirFn(ptr: *anyopaque, _: std.Io, path: []const u8) anyerror!void {
        const self: *MockIo = @ptrCast(@alignCast(ptr));
        try self.dirs.append(self.a, try self.a.dupe(u8, path));
    }
    fn writeFn(ptr: *anyopaque, _: std.Io, path: []const u8, data: []const u8) anyerror!void {
        const self: *MockIo = @ptrCast(@alignCast(ptr));
        try self.files.put(try self.a.dupe(u8, path), try self.a.dupe(u8, data));
    }
};

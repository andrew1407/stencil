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
const pipeline = @import("../pipeline.zig");
const args = @import("../args.zig");
const confine = @import("../safety/confine.zig");
const fetchPool = @import("../net/fetchPool.zig");
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
const max_name_pattern = filters.max_name_pattern;
const parseMedia = page.parseMedia;
const Sniff = sniffer.Sniff;
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

pub fn runImpl(gpa: std.mem.Allocator, io: std.Io, opts: args.Options, deps: Deps) !void {
    var arena_state = std.heap.ArenaAllocator.init(gpa);
    defer arena_state.deinit();
    const arena = arena_state.allocator();

    const site = opts.source_site.?;
    const host_raw = net.hostOf(site) orelse {
        deps.err(arena, "could not parse a host from URL '{s}'\n", .{site});
        return error.BadSourceUrl;
    };
    const dir = opts.output orelse ".";
    if (pipeline.hasParentTraversal(dir) or (opts.confine_output and confine.escapes(io, std.Io.Dir.cwd(), dir))) {
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
        if (opts.confine_output and confine.escapesThroughLink(io, std.Io.Dir.cwd(), path)) {
            deps.err(arena, "--confine-output: refusing to write through a link out of the working directory: '{s}'\n", .{path});
            continue;
        }
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

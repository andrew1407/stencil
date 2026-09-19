//! Bounded concurrent fetching, plus the per-thread body scratch the HTTP client borrows
//! (net.zig). A port of pystencil's `_net._fetch_all`: jobs run on at most `max_workers`
//! concurrent tasks and their results land in SUBMISSION order, so a scrape writes and
//! prints byte-for-byte what the serial loop did. Nothing here touches shared mutable
//! state — a worker's only outputs are the bytes in its own arena and its `Fetched` row.
const std = @import("std");

/// Upper bound on concurrent fetches in one batch: the jobs are I/O-bound, so a small pool turns N
/// serial round-trips into roughly one without antisocial socket counts. = pystencil MAX_FETCH_WORKERS.
pub const max_workers = 8;

// One reusable body scratch per THREAD rather than one per request: eight concurrent fetches would
// otherwise map eight 64 MiB buffers. Pages commit lazily, so a thread pays for what it read.
threadlocal var scratch: ?[]u8 = null;

/// A page-allocated buffer of at least `min_len` bytes, cached for this thread; null on OOM.
pub fn bodyScratch(min_len: usize) ?[]u8 {
    if (scratch) |s| {
        if (s.len >= min_len) return s;
        releaseScratch();
    }
    scratch = std.heap.page_allocator.alloc(u8, min_len) catch null;
    return scratch;
}

pub fn releaseScratch() void {
    if (scratch) |s| std.heap.page_allocator.free(s);
    scratch = null;
}

/// The `net.fetch` shape, as the injectable seam scrape already passes around.
pub const FetchFn = *const fn (*anyopaque, std.mem.Allocator, std.Io, []const u8, bool) anyerror![]u8;

/// One URL to fetch. `strict` blocks loopback too (see scrape's subStrict).
pub const Job = struct { url: []const u8, strict: bool };

/// What one job produced: the bytes, or the error to report. Never both.
pub const Fetched = struct { bytes: ?[]const u8 = null, err: ?anyerror = null };

/// A run of fetches and the arenas holding what they downloaded. An ArenaAllocator is not shareable,
/// so every job gets its own, allocated once (the allocator captures its address) and kept to `deinit`.
pub const Batch = struct {
    gpa: std.mem.Allocator,
    arenas: []std.heap.ArenaAllocator,
    results: []Fetched,

    pub fn init(gpa: std.mem.Allocator, n: usize) !Batch {
        const arenas = try gpa.alloc(std.heap.ArenaAllocator, n);
        errdefer gpa.free(arenas);
        for (arenas) |*a| a.* = std.heap.ArenaAllocator.init(gpa);
        const results = gpa.alloc(Fetched, n) catch |e| {
            for (arenas) |*a| a.deinit();
            return e;
        };
        @memset(results, .{});
        return .{ .gpa = gpa, .arenas = arenas, .results = results };
    }

    pub fn deinit(self: *Batch) void {
        for (self.arenas) |*a| a.deinit();
        self.gpa.free(self.arenas);
        self.gpa.free(self.results);
    }

    /// Fetch every job, filling `results[i]` from `jobs[i]`; serial where the Io cannot spawn. One-shot
    /// path only — never under the full-screen console, whose sink repaints from the terminal's thread.
    pub fn run(self: *Batch, io: std.Io, ctx: *anyopaque, f: FetchFn, jobs: []const Job) void {
        std.debug.assert(jobs.len == self.results.len);
        defer releaseScratch();
        if (jobs.len < 2) {
            for (jobs, 0..) |job, i| self.results[i] = one(f, ctx, self.arenas[i].allocator(), io, job);
            return;
        }
        var futures: [max_workers]std.Io.Future(Fetched) = undefined;
        var base: usize = 0;
        while (base < jobs.len) {
            const end = @min(base + max_workers, jobs.len);
            var spawned: usize = 0;
            while (base + spawned < end) : (spawned += 1) {
                const i = base + spawned;
                futures[spawned] = io.concurrent(one, .{ f, ctx, self.arenas[i].allocator(), io, jobs[i] }) catch break;
            }
            // Whatever the Io would not spawn runs here, while the spawned ones are in flight.
            for (base + spawned..end) |i| self.results[i] = one(f, ctx, self.arenas[i].allocator(), io, jobs[i]);
            for (0..spawned) |k| self.results[base + k] = futures[k].await(io);
            base = end;
        }
    }
};

/// Fetch `jobs` and hand back the finished batch — the usual one-shot form. The caller owns
/// it: `results[i]` belongs to `jobs[i]`, and the bytes live until `deinit`.
pub fn fetchAll(gpa: std.mem.Allocator, io: std.Io, ctx: *anyopaque, f: FetchFn, jobs: []const Job) !Batch {
    var batch = try Batch.init(gpa, jobs.len);
    batch.run(io, ctx, f, jobs);
    return batch;
}

fn one(f: FetchFn, ctx: *anyopaque, a: std.mem.Allocator, io: std.Io, job: Job) Fetched {
    const bytes = f(ctx, a, io, job.url, job.strict) catch |e| return .{ .err = e };
    return .{ .bytes = bytes };
}

const testing = std.testing;

// A job that records which thread ran it and echoes the URL back as its bytes.
const Recorder = struct {
    calls: std.atomic.Value(usize) = .init(0),

    fn fetch(ptr: *anyopaque, a: std.mem.Allocator, _: std.Io, url: []const u8, _: bool) anyerror![]u8 {
        const self: *Recorder = @ptrCast(@alignCast(ptr));
        _ = self.calls.fetchAdd(1, .monotonic);
        if (std.mem.endsWith(u8, url, ".bad")) return error.HttpFailed;
        return a.dupe(u8, url);
    }
};

test "Batch.run keeps submission order and reports per-job failures" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    // More jobs than workers, so the batching loop runs more than once.
    const n = max_workers * 2 + 3;
    var jobs: [n]Job = undefined;
    var urls: [n][16]u8 = undefined;
    for (&jobs, 0..) |*j, i| {
        const ext = if (i % 5 == 0) ".bad" else ".png";
        j.* = .{ .url = std.fmt.bufPrint(&urls[i], "u{d}{s}", .{ i, ext }) catch unreachable, .strict = false };
    }

    var rec = Recorder{};
    var batch = try Batch.init(testing.allocator, n);
    defer batch.deinit();
    batch.run(io, @ptrCast(&rec), Recorder.fetch, &jobs);

    try testing.expectEqual(@as(usize, n), rec.calls.load(.monotonic));
    for (batch.results, 0..) |r, i| {
        if (i % 5 == 0) {
            try testing.expect(r.bytes == null);
            try testing.expectEqual(anyerror.HttpFailed, r.err.?);
        } else {
            try testing.expectEqualStrings(jobs[i].url, r.bytes.?); // result i came from job i
            try testing.expect(r.err == null);
        }
    }
}

test "Batch.run handles the empty and single-job cases" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var rec = Recorder{};
    var empty = try Batch.init(testing.allocator, 0);
    defer empty.deinit();
    empty.run(io, @ptrCast(&rec), Recorder.fetch, &.{});
    try testing.expectEqual(@as(usize, 0), rec.calls.load(.monotonic));

    var single = try Batch.init(testing.allocator, 1);
    defer single.deinit();
    single.run(io, @ptrCast(&rec), Recorder.fetch, &.{.{ .url = "solo.png", .strict = true }});
    try testing.expectEqualStrings("solo.png", single.results[0].bytes.?);
}

test "bodyScratch caches per thread and grows on demand" {
    defer releaseScratch();
    const small = bodyScratch(1024).?;
    try testing.expectEqual(small.ptr, bodyScratch(1024).?.ptr); // same buffer, no remap
    try testing.expect(bodyScratch(1 << 20).?.len >= 1 << 20); // too small → replaced
}

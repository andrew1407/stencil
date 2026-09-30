//! `--probe -i <path|url>`: format, pixel size, alpha and byte size read from the header, never
//! a full decode; a video adds its duration and frame count from ffprobe, or just its size from
//! ffmpeg's first frame when ffprobe is missing.
const std = @import("std");
const args = @import("../args.zig");
const image = @import("../media/image.zig");
const video = @import("../media/video.zig");
const net = @import("../net.zig");
const report = @import("../app/report.zig");
const scrape = @import("../scrape.zig");
const sources = @import("../pipeline/sources.zig");
const header = @import("header.zig");

/// Read from the head of a local file: well past a JPEG's EXIF block to its frame header.
const HEAD_BYTES: usize = 1 << 20;

pub const Info = struct {
    format: ?[]const u8,
    width: u32,
    height: u32,
    alpha: ?bool = null, // null where the header cannot say
    bytes: ?u64 = null, // null for a video URL, which ffprobe reads itself
    video: bool = false,
    duration_ms: ?u64 = null,
    frames: ?u64 = null,
};

pub fn run(gpa: std.mem.Allocator, io: std.Io, out: *std.Io.Writer, opts: args.Options) !void {
    const input = opts.input orelse {
        report.err("--probe needs -i <path|url>\n", .{});
        return error.NoSource;
    };
    if (opts.server != null) {
        report.err("--probe reads a local path or URL, not a server project\n", .{});
        return error.NoSource;
    }
    if (net.hasForeignScheme(input)) {
        report.err("unsupported URL scheme in '{s}' — pass an http(s) URL or a local path\n", .{input});
        return error.UnsupportedScheme;
    }
    const info = if (video.looksLikeVideo(input)) try probeVideo(gpa, io, input) else try probeImage(gpa, io, input);
    try writeJson(out, info);
    try out.flush();
}

fn probeImage(gpa: std.mem.Allocator, io: std.Io, input: []const u8) !Info {
    var total: u64 = 0;
    var head: []u8 = undefined;
    if (net.isUrl(input)) {
        head = net.fetch(gpa, io, input, false) catch |e| return sources.mapMediaError(e);
        total = head.len;
    } else {
        head = try readHead(gpa, io, input, &total);
    }
    defer gpa.free(head);
    const h = header.read(head, input) orelse {
        report.err("could not read an image header from '{s}'\n", .{input});
        return error.NotAnImage;
    };
    return .{ .format = h.format, .width = h.width, .height = h.height, .alpha = h.alpha, .bytes = total };
}

/// The first HEAD_BYTES of a local file, and its whole size in `total`.
fn readHead(gpa: std.mem.Allocator, io: std.Io, input: []const u8, total: *u64) ![]u8 {
    const path = try sources.expandHome(gpa, input);
    defer gpa.free(path);
    var file = std.Io.Dir.cwd().openFile(io, path, .{}) catch |e| {
        report.err("cannot read '{s}': {s}\n", .{ path, @errorName(e) });
        return e;
    };
    defer file.close(io);
    total.* = (try file.stat(io)).size;
    const head = try gpa.alloc(u8, @min(HEAD_BYTES, total.*));
    errdefer gpa.free(head);
    var buf: [4096]u8 = undefined;
    var reader = file.readerStreaming(io, &buf);
    const n = try reader.interface.readSliceShort(head);
    if (n == head.len) return head;
    return gpa.realloc(head, n);
}

fn probeVideo(gpa: std.mem.Allocator, io: std.Io, input: []const u8) !Info {
    var info = Info{ .format = videoExt(input), .width = 0, .height = 0, .video = true };
    if (!net.isUrl(input)) {
        const path = try sources.expandHome(gpa, input);
        defer gpa.free(path);
        const st = std.Io.Dir.cwd().statFile(io, path, .{}) catch |e| {
            report.err("cannot read '{s}': {s}\n", .{ path, @errorName(e) });
            return e;
        };
        info.bytes = st.size;
    }
    if (video.probe(gpa, io, input)) |s| {
        info.width = s.width;
        info.height = s.height;
        info.duration_ms = s.duration_ms;
        info.frames = s.frames;
        return info;
    } else |e| if (e != video.Error.FfprobeMissing) return e;
    // Without ffprobe, ffmpeg's first frame still carries the size.
    const png = video.extractFrame(gpa, io, input, 0) catch |e| return sources.mapMediaError(e);
    defer gpa.free(png);
    const s = scrape.sniff(png) orelse return error.NotAnImage;
    info.width = s.width;
    info.height = s.height;
    return info;
}

/// The extension a video path or URL ends in (`?query`/`#fragment` trimmed).
fn videoExt(input: []const u8) ?[]const u8 {
    const end = std.mem.indexOfAny(u8, input, "?#") orelse input.len;
    return image.extOf(input[0..end]);
}

/// One JSON object and a newline; `durationMs` and `frames` only for a video.
pub fn writeJson(out: *std.Io.Writer, info: Info) !void {
    var low: [16]u8 = undefined;
    const fmt: ?[]const u8 = if (info.format) |f| (if (f.len <= low.len) std.ascii.lowerString(low[0..f.len], f) else f) else null;
    var js: std.json.Stringify = .{ .writer = out };
    try js.beginObject();
    try js.objectField("format");
    try js.write(fmt);
    try js.objectField("width");
    try js.write(info.width);
    try js.objectField("height");
    try js.write(info.height);
    try js.objectField("alpha");
    try js.write(info.alpha);
    try js.objectField("bytes");
    try js.write(info.bytes);
    if (info.video) {
        try js.objectField("durationMs");
        try js.write(info.duration_ms);
        try js.objectField("frames");
        try js.write(info.frames);
    }
    try js.endObject();
    try out.writeByte('\n');
}

const testing = std.testing;

test "probe: the fixture's header, as one JSON line" {
    const gpa = testing.allocator;
    var threaded = std.Io.Threaded.init(gpa, .{});
    defer threaded.deinit();
    var out: std.Io.Writer.Allocating = .init(gpa);
    defer out.deinit();
    try run(gpa, threaded.io(), &out.writer, .{ .probe = true, .input = "../common/samples/sample.png" });

    const text = out.written();
    try testing.expect(std.mem.endsWith(u8, text, "}\n"));
    try testing.expect(std.mem.startsWith(u8, text, "{\"format\":\"png\",\"width\":16,\"height\":12,\"alpha\":"));
    try testing.expect(std.mem.indexOf(u8, text, "durationMs") == null);
    try testing.expectError(error.NoSource, run(gpa, threaded.io(), &out.writer, .{ .probe = true }));
}

test {
    _ = header;
}

test "writeJson: a video carries its duration and frame count, null when unknown" {
    var out: std.Io.Writer.Allocating = .init(testing.allocator);
    defer out.deinit();
    try writeJson(&out.writer, .{ .format = "MP4", .width = 32, .height = 24, .video = true, .duration_ms = 1000 });
    try testing.expectEqualStrings(
        "{\"format\":\"mp4\",\"width\":32,\"height\":24,\"alpha\":null,\"bytes\":null,\"durationMs\":1000,\"frames\":null}\n",
        out.written(),
    );
}

test "probe: a real video through ffprobe (skipped without ffmpeg)" {
    const gpa = testing.allocator;
    const io = testing.io; // carries the runner's environment, so a spawn finds ffmpeg on PATH
    const clip = "stencil_probe_clip.mp4";
    const made = std.process.run(gpa, io, .{ .argv = &.{
        "ffmpeg", "-nostdin", "-loglevel", "error",   "-y", "-f", "lavfi", "-i", "testsrc=size=32x24:rate=10",
        "-t",     "1",        "-pix_fmt",  "yuv420p", clip,
    } }) catch return error.SkipZigTest;
    gpa.free(made.stdout);
    gpa.free(made.stderr);
    defer std.Io.Dir.cwd().deleteFile(io, clip) catch {};
    const info = probeVideo(gpa, io, clip) catch return error.SkipZigTest;
    try testing.expectEqual(@as(u32, 32), info.width);
    try testing.expectEqual(@as(u32, 24), info.height);
    try testing.expectEqual(@as(?u64, 10), info.frames);
    try testing.expectEqualStrings("mp4", info.format.?);
    try testing.expect(info.bytes.? > 0);
}

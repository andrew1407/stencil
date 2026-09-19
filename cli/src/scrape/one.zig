//! `/source-upload` (alias `/scrape`): the console's single-item form — scrape, filter,
//! and decode the one item the user asked for.
const std = @import("std");
const net = @import("../net.zig");
const image = @import("../image.zig");
const report = @import("../report.zig");
const windowing = @import("window.zig");
const sniffer = @import("sniff.zig");
const page = @import("html.zig");
const filter = @import("filter.zig");
const dimensionPass = filter.dimensionPass;
const formatPass = filter.formatPass;
const parseMedia = page.parseMedia;
const Sniff = sniffer.Sniff;
const sniff = sniffer.sniff;
const formatFor = windowing.formatFor;
const subStrict = windowing.subStrict;

pub const ConsoleOpts = struct {
    url: []const u8,
    index: u32 = 0,
    format: []const u8 = "all",
    min_width: ?u32 = null,
    max_width: ?u32 = null,
    min_height: ?u32 = null,
    max_height: ?u32 = null,
    /// Optional custom label for the loaded image ("" = derive from the media URL). Consumed
    /// by the console handler (doSourceUpload), not by scrapeOne — it does not affect scraping.
    name: []const u8 = "",
};

/// Result of a console scrape: an owned decoded image, the chosen media URL (owned), and the
/// format to save it back as. Caller loads it into the session and frees `url`.
pub const Loaded = struct { img: image.Rgba8, url: []u8, fmt: image.Format };

/// Scrape `o.url`, filter to image-category items (img / bg / poster) by format + dimension, pick the
/// item at 0-based `o.index` of the ordered list, download and decode it. Prints on failure.
pub fn scrapeOne(gpa: std.mem.Allocator, io: std.Io, o: ConsoleOpts) !Loaded {
    var arena_state = std.heap.ArenaAllocator.init(gpa);
    defer arena_state.deinit();
    const arena = arena_state.allocator();

    const html = try net.fetch(gpa, io, o.url, false); // user-named page URL
    defer gpa.free(html);
    const medias = try parseMedia(arena, html, o.url);
    const page_host = net.hostOf(o.url) orelse ""; // for the sub-resource same-host rule

    const dim_active = o.min_width != null or o.max_width != null or o.min_height != null or o.max_height != null;

    // Only a dimension filter forces a per-candidate fetch (to measure each, mirroring pystencil's
    // scan_page); without one the category+format list fixes the pick, so ONLY the chosen item is fetched.
    var matches: usize = 0;
    for (medias) |m| {
        const cat = m.category();
        const is_image = std.mem.eql(u8, cat, "img") or std.mem.eql(u8, cat, "background") or std.mem.eql(u8, cat, "poster");
        if (!is_image) continue;
        if (!formatPass(m, o.format)) continue;

        var dims: ?Sniff = null;
        var cached: ?[]const u8 = null; // measurement bytes, reused for the pick (no double fetch)
        if (dim_active) {
            const bytes = net.fetch(arena, io, m.url, subStrict(m.url, page_host)) catch |e| {
                report.err("could not fetch {s} ({s})\n", .{ m.url, @errorName(e) });
                continue;
            };
            cached = bytes;
            dims = sniff(bytes);
            if (!dimensionPass(dims, o.min_width, o.max_width, o.min_height, o.max_height)) continue;
        }

        if (matches == o.index) {
            const bytes = cached orelse (net.fetch(arena, io, m.url, subStrict(m.url, page_host)) catch |e| {
                report.err("could not fetch {s} ({s})\n", .{ m.url, @errorName(e) });
                return e;
            });
            if (dims == null) dims = sniff(bytes);
            const img = image.decode(gpa, bytes) catch |e| {
                report.err("could not decode an image from '{s}' ({s})\n", .{ m.url, @errorName(e) });
                return e;
            };
            var fbuf: [16]u8 = undefined;
            const ext = formatFor(&fbuf, m, dims);
            const fmt = image.formatFromExt(ext) orelse .png;
            return .{ .img = img, .url = try gpa.dupe(u8, m.url), .fmt = fmt };
        }
        matches += 1;
    }
    report.err("no scrape match at index {d} for {s}\n", .{ o.index, o.url });
    return error.NoMediaMatched;
}

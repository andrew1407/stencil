//! `/upload`, `/source-upload` and `/blank`: everything that brings a NEW image into the
//! session. Loading a `.stencil` project goes through project.zig; the scrape grammar is
//! parsed here and executed by scrape.zig.
const std = @import("std");
const pipeline = @import("../../pipeline.zig");
const net = @import("../../net.zig");
const scrape = @import("../../scrape.zig");
const logo = @import("../../logo.zig");
const core = @import("../../core.zig");
const commands = @import("../commands.zig");
const project = @import("../../project.zig");
const msg = @import("../../messages.zig");
const ui = @import("../ui.zig");
const Session = @import("../session.zig").Session;
const attachments = @import("../attachments.zig");

pub fn doUpload(session: *Session, io: std.Io, arg: []const u8) !void {
    if (arg.len == 0) {
        // A bare /upload takes the CLIPBOARD's picture when there is one — the same image
        // Ctrl-V attaches — so a copied screenshot needs no path typed at all.
        if (try attachments.clipboardToImage(session, io, true)) return;
        logo.err(msg.upload_needs_path, .{});
        return;
    }
    // A whole .stencil project loads its image + layout (crop/rotation/filter/lines) at once.
    if (project.isStencilPath(arg)) return openProject(session, io, arg);
    const src = pipeline.acquireInput(session.gpa, io, arg, 0) catch return; // message already printed
    // §2.1: the uploads of one turn are its attachments — a later /prompt sends them all
    // and an `image` op indexes them. Best-effort: a copy we can't afford just isn't one.
    const att_bytes: ?[]u8 = session.gpa.dupe(u8, src.bytes) catch null;
    errdefer if (att_bytes) |b| session.gpa.free(b);
    const temp = net.isUrl(arg);
    try session.loadImage(src.img, arg, temp, src.default_fmt, src.bytes);
    if (att_bytes) |b| session.addAttachment(arg, b, src.default_fmt, temp) catch {};
    ui.redraw(session);
}

/// `/upload <file>.stencil` — load a portable project: decode its embedded ORIGINAL image and
/// adopt its layout (crop/rotation/filter/lines) so the view matches the browser/desktop editors.
pub fn openProject(session: *Session, io: std.Io, path: []const u8) !void {
    var proj = project.loadInto(session, io, path) catch return; // message already printed
    proj.deinit();
    ui.redraw(session);
}

/// `/source-upload <url> [index=0] [format=all] [minW=-1] [maxW=-1] [minH=-1] [maxH=-1]`
/// (alias `/scrape`) — scrape a page, filter by format + dimensions, load the `index`-th item.
pub fn doSourceUpload(session: *Session, io: std.Io, arg: []const u8) !void {
    if (arg.len == 0) {
        logo.err(msg.source_upload_needs_url, .{});
        return;
    }
    const o = parseSourceUpload(arg) orelse {
        logo.err(msg.source_upload_usage, .{});
        return;
    };
    // The fetch + download can take a moment; announce it up front (parity with the one-shot
    // scrape's leading line and pystencil's _cmd_source_upload) so the console isn't silent.
    logo.print(msg.scraping, .{o.url});
    const loaded = scrape.scrapeOne(session.gpa, io, o) catch return; // message already printed
    defer session.gpa.free(loaded.url);
    // A `name=` token overrides the URL-derived label (parity with pystencil's name=).
    const label = if (o.name.len != 0) o.name else loaded.url;
    try session.loadImage(loaded.img, label, true, loaded.fmt, null);
    ui.redraw(session);
}

/// Parse the `/source-upload` positional grammar; null on a malformed token. `-1` min/max bounds
/// become unset, a bare `all` format means any, and `name=<label>` may appear anywhere after the URL.
fn parseSourceUpload(arg: []const u8) ?scrape.ConsoleOpts {
    var it = std.mem.tokenizeAny(u8, arg, " \t");
    const url = it.next() orelse return null;
    var o = scrape.ConsoleOpts{ .url = url };
    // Positionals: [index] [format] [minW] [maxW] [minH] [maxH]; a `name=` token is pulled
    // out first (anywhere) so it doesn't consume a positional slot.
    var pos: [6]?[]const u8 = .{ null, null, null, null, null, null };
    var np: usize = 0;
    while (it.next()) |t| {
        if (std.mem.startsWith(u8, t, "name=")) {
            o.name = t["name=".len..];
            continue;
        }
        if (np >= pos.len) return null; // trailing junk
        pos[np] = t;
        np += 1;
    }
    if (pos[0]) |t| o.index = std.fmt.parseInt(u32, t, 10) catch return null;
    if (pos[1]) |t| o.format = t;
    o.min_width = parseBound(pos[2]) catch return null;
    o.max_width = parseBound(pos[3]) catch return null;
    o.min_height = parseBound(pos[4]) catch return null;
    o.max_height = parseBound(pos[5]) catch return null;
    return o;
}

/// A `/source-upload` dimension bound token: `-1` (or absent) → unset (null); else a u32.
fn parseBound(tok: ?[]const u8) !?u32 {
    const t = tok orelse return null;
    const v = try std.fmt.parseInt(i64, t, 10);
    if (v < 0) return null;
    return @intCast(v);
}

pub fn doBlank(session: *Session, arg: []const u8) !void {
    var blank = commands.parseBlank(arg) orelse {
        logo.err(msg.blank_usage, .{});
        return;
    };
    // Capture the session's /format pick before the load wipes it (loadImage → clearAll →
    // clearFormat). The canonical slice is static (core-owned), so it survives the load.
    const prev_page: ?[]const u8 = core.canonicalPageFormat(session.page_size);
    const prev_custom = std.ascii.eqlIgnoreCase(session.page_size, "custom");
    const prev_w = session.custom_page_w;
    const prev_h = session.custom_page_h;
    // A bare size (no format, no dims) defaults to the session's picked page format (set via
    // /format or a fetched layout).
    var custom_w: f64 = 0;
    var custom_h: f64 = 0;
    if (blank.page == null and blank.width == null and session.page_size.len != 0) {
        if (prev_custom) {
            custom_w = prev_w;
            custom_h = prev_h;
            if (custom_w > 0 and custom_h > 0) {
                const s = pipeline.blankSizeFor(null, custom_w, custom_h);
                blank.width = @intCast(s.w);
                blank.height = @intCast(s.h);
            }
        } else {
            blank.page = prev_page;
        }
    }
    const img = try pipeline.acquireBlank(session.gpa, blank);
    try session.loadImage(img, "blank", true, .png, null);
    // Keep the page the blank was created on as the session's pick; explicit dims size the
    // blank but keep the previous /format (matching the Telegram bot's session PageFormat).
    if (blank.page) |p| {
        session.setPageSize(p) catch {};
    } else if (custom_w > 0 and custom_h > 0) {
        session.setPageSize("custom") catch {};
        session.custom_page_w = custom_w;
        session.custom_page_h = custom_h;
    } else if (prev_page) |p| {
        session.setPageSize(p) catch {};
    } else if (prev_custom and prev_w > 0 and prev_h > 0) {
        session.setPageSize("custom") catch {};
        session.custom_page_w = prev_w;
        session.custom_page_h = prev_h;
    }
    ui.redraw(session);
}

const testing = std.testing;

test "parseSourceUpload: positional grammar, -1 = unset, junk rejected" {
    const o = parseSourceUpload("https://x/ 2 png 100 -1 50 800").?;
    try testing.expectEqualStrings("https://x/", o.url);
    try testing.expectEqual(@as(u32, 2), o.index);
    try testing.expectEqualStrings("png", o.format);
    try testing.expectEqual(@as(u32, 100), o.min_width.?);
    try testing.expect(o.max_width == null); // -1 → unset
    try testing.expectEqual(@as(u32, 50), o.min_height.?);
    try testing.expectEqual(@as(u32, 800), o.max_height.?);

    // Bare URL → defaults (index 0, all formats, all bounds unset).
    const d = parseSourceUpload("https://x/").?;
    try testing.expectEqual(@as(u32, 0), d.index);
    try testing.expectEqualStrings("all", d.format);
    try testing.expect(d.min_width == null and d.max_height == null);

    try testing.expect(parseSourceUpload("") == null); // no url
    try testing.expect(parseSourceUpload("https://x/ notanumber") == null); // bad index
}

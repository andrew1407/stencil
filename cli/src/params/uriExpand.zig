//! The one-shot half of the launch URI (uri.zig): spotting one among the arguments and expanding
//! it into `Options` as the flags it stands for — `source` is `-i`, `frame` is `-f`, `blank` with
//! `width`/`height` is `--blank`, `layout` is `-l`, `server`+`id` is `--server -i`, a paired
//! `token` is `--token`, `name` is `--remote-name`. The rest is the console's (apps/inbound.zig).
const std = @import("std");
const uri = @import("uri.zig");
const keys = @import("uriKeys.zig");
const options = @import("options.zig");
const testing = std.testing;

/// An http(s) or stencil: URL carrying a key of the grammar, judged without allocating: a plain
/// picture URL with tracking parameters is not one.
pub fn looksLikeLaunchUri(arg: []const u8) bool {
    const a = std.mem.trim(u8, arg, " \t");
    const web = std.ascii.startsWithIgnoreCase(a, "http://") or std.ascii.startsWithIgnoreCase(a, "https://");
    if (!web and !std.ascii.startsWithIgnoreCase(a, "stencil:")) return false;
    const q = std.mem.indexOfAny(u8, a, "?#") orelse return false;
    var it = std.mem.tokenizeAny(u8, a[q + 1 ..], "&?#");
    while (it.next()) |part| {
        const k = part[0 .. std.mem.indexOfScalar(u8, part, '=') orelse part.len];
        var buf: [48]u8 = undefined;
        if (k.len > buf.len) continue;
        const low = std.ascii.lowerString(&buf, k);
        if (keys.canonicalOf(low)) |c| if (!std.mem.eql(u8, c, "open")) return true;
    }
    return false;
}

/// Expands a launch URI standing in `-i` (or the positional) into the flags it means; true when
/// it did. A URI naming no picture is left alone, so an image URL is fetched as itself.
pub fn expandOptions(a: std.mem.Allocator, opts: *options.Options) !bool {
    const input = opts.input orelse return false;
    if (!looksLikeLaunchUri(input)) return false;
    const q = (try uri.parse(a, input)) orelse return false;
    const picture = q.picture orelse return false;
    // The link is one source; `--blank` or `--source-site` beside it is the parser's DuplicateSource.
    if (opts.blank != null or opts.source_site != null) return error.DuplicateSource;
    switch (picture) {
        .source => |s| {
            opts.input = s.url;
            if (s.frameIndex()) |f| opts.frame = f;
        },
        .blank => |b| {
            opts.input = null;
            const size = b.sizePx();
            opts.blank = .{ .width = if (size) |s| s[0] else null, .height = if (size) |s| s[1] else null, .color = b.color };
        },
        .server => |s| {
            opts.server = s.url;
            opts.input = s.id;
        },
        // A bare ref without a server names a local project, which the one-shot has none of.
        .project => |p| {
            opts.server = q.projectOrigin(p.server) orelse return false;
            opts.input = p.ref;
        },
    }
    if (q.connect.len > 0 and q.connect[0].token.len > 0) opts.token = q.connect[0].token;
    if (q.layout) |l| opts.layout = l.text();
    if (q.name) |n| opts.remote_name = n;
    return true;
}

test "looksLikeLaunchUri: a grammar key after ? or #, never a plain picture URL" {
    try testing.expect(looksLikeLaunchUri("https://app.example/?source=https://x/a.png"));
    try testing.expect(looksLikeLaunchUri("stencil://open?src=https%3A%2F%2Fx%2Fa.png"));
    try testing.expect(looksLikeLaunchUri("https://app.example/#blank=red"));
    try testing.expect(!looksLikeLaunchUri("https://cdn.example/a.png?v=3&utm_source=tg"));
    try testing.expect(!looksLikeLaunchUri("https://app.example/?open=p_1"));
    try testing.expect(!looksLikeLaunchUri("photo.png"));
    try testing.expect(!looksLikeLaunchUri("file:///tmp/x?source=y"));
}

test "expandOptions: a source URI becomes -i -f -l, a blank one --blank, a server one --server -i --token" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();

    var src = options.Options{ .input = "https://app.example/?source=https://x/clip.mp4&frame=12&layout=https://x/l.json&name=Clip" };
    try testing.expect(try expandOptions(a, &src));
    try testing.expectEqualStrings("https://x/clip.mp4", src.input.?);
    try testing.expectEqual(@as(u32, 12), src.frame);
    try testing.expectEqualStrings("https://x/l.json", src.layout.?);
    try testing.expectEqualStrings("Clip", src.remote_name.?);

    var blank = options.Options{ .input = "https://app.example/?blank=%23336699&width=890&height=243" };
    try testing.expect(try expandOptions(a, &blank));
    try testing.expect(blank.input == null);
    try testing.expectEqual(@as(u32, 890), blank.blank.?.width.?);
    try testing.expectEqual(@as(u32, 243), blank.blank.?.height.?);
    try testing.expectEqualStrings("#336699", blank.blank.?.color);

    var server = options.Options{ .input = "https://app.example/#server=http://h:8090&id=p_1&connect=http://h:8090&token=tok" };
    try testing.expect(try expandOptions(a, &server));
    try testing.expectEqualStrings("http://h:8090", server.server.?);
    try testing.expectEqualStrings("p_1", server.input.?);
    try testing.expectEqualStrings("tok", server.token.?);

    var project = options.Options{ .input = "https://app.example/?project=localhost:8080/projects/Sketch" };
    try testing.expect(try expandOptions(a, &project));
    try testing.expectEqualStrings("localhost:8080", project.server.?);
    try testing.expectEqualStrings("Sketch", project.input.?);
    var bare = options.Options{ .input = "https://app.example/?connect=http://h:8090&project=p_1" };
    try testing.expect(try expandOptions(a, &bare));
    try testing.expectEqualStrings("http://h:8090", bare.server.?);
    try testing.expectEqualStrings("p_1", bare.input.?);
    var local = options.Options{ .input = "https://app.example/?project=Sketch" };
    try testing.expect(!try expandOptions(a, &local));
}

test "a launch URI positional is the input, never the output, and clashes with --blank" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const parse = @import("parse.zig").parse;
    const argv = [_][:0]const u8{ "https://app.example/?source=https://x/a.png&frame=2", "out.png" };
    var o = try parse(&argv);
    try testing.expectEqualStrings("https://app.example/?source=https://x/a.png&frame=2", o.input.?);
    try testing.expectEqualStrings("out.png", o.output.?);
    try testing.expect(try expandOptions(arena.allocator(), &o));
    try testing.expectEqualStrings("https://x/a.png", o.input.?);
    const dup = [_][:0]const u8{ "--blank", "10", "10", "stencil://open?src=https%3A%2F%2Fx%2Fa.png" };
    var d = try parse(&dup);
    try testing.expectError(error.DuplicateSource, expandOptions(arena.allocator(), &d));
}

test "expandOptions: a picture URL with grammar-looking parameters but no picture stays an input" {
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    var o = options.Options{ .input = "https://cdn.example/a.png?width=800&name=big" };
    try testing.expect(!try expandOptions(arena.allocator(), &o));
    try testing.expectEqualStrings("https://cdn.example/a.png?width=800&name=big", o.input.?);
    var plain = options.Options{ .input = "photo.png" };
    try testing.expect(!try expandOptions(arena.allocator(), &plain));
}

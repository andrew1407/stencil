// Walks the shared image-header corpus (browser/js/config/fixtures/imageHeader/cases.json) against
// the cli's one sniffer, scrape/sniff.zig — the scraper's dimension filter and `--probe` both read
// through it. The corpus says "jpeg" where the cli says "jpg".
const std = @import("std");
const sniff = @import("../../src/scrape/sniff.zig");
const fx = @import("../fixture_corpus.zig");
const testing = std.testing;

test "image-header corpus: every case measures as pinned, or not at all" {
    var w = fx.Walk.start();
    defer w.stop();
    const a = w.alloc();
    const cases = try w.cases("fixtures/imageHeader/cases.json");
    try testing.expect(cases.len >= 40);
    for (cases) |c| {
        const name = fx.memberStr(c, "name").?;
        const b64 = fx.memberStr(c, "base64") orelse "";
        const d = std.base64.standard.Decoder;
        const bytes = try a.alloc(u8, try d.calcSizeForSlice(b64));
        try d.decode(bytes, b64);
        w.walked += 1;
        const got = sniff.sniff(bytes);
        const expect = fx.member(c, "expect").?;
        if (expect == .null) {
            if (got) |g| w.fail("imageHeader {s}: want null, cli says {s} {d}x{d}\n", .{ name, g.fmt, g.width, g.height });
            continue;
        }
        const g = got orelse {
            w.fail("imageHeader {s}: want a size, cli says null\n", .{name});
            continue;
        };
        const fmt = if (std.mem.eql(u8, g.fmt, "jpg")) "jpeg" else g.fmt;
        const wi: u32 = @intCast(fx.member(expect, "width").?.integer);
        const he: u32 = @intCast(fx.member(expect, "height").?.integer);
        if (!std.mem.eql(u8, fmt, fx.memberStr(expect, "format").?) or g.width != wi or g.height != he)
            w.fail("imageHeader {s}: want {s} {d}x{d}, cli says {s} {d}x{d}\n", .{ name, fx.memberStr(expect, "format").?, wi, he, fmt, g.width, g.height });
    }
    try w.report("imageHeader");
}

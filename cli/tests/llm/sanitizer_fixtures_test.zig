// Walks the shared provider-error sanitizer corpus (llm/fixtures/sanitizer/cases.json) against the
// cli's real sanitizer (llm.sanitizeDetail — the one errorDetail/finish use). Corpus expectations are
// the BROWSER's output, so DIVERGENCE(...) cases (and measured cli drift pinned in
// fixture_overrides.json) are recomputed locally: the walker enforces the invariants instead — no URL,
// no 24+ token run, output within the cli's byte-counted cap (200 bytes + ellipsis).
const std = @import("std");
const llm = @import("../../src/llm.zig");
const fx = @import("../fixture_corpus.zig");
const testing = std.testing;

/// llm.zig's DetailBuf: detail_limit (200) + "…".len bytes.
const DetailBuf = [200 + "…".len]u8;

fn isTokenChar(c: u8) bool {
    return std.ascii.isAlphanumeric(c) or c == '.' or c == '_' or c == '-';
}

/// The sanitizer invariants every output must keep, whatever the exact text.
fn checkInvariants(out: []const u8) !void {
    try testing.expect(out.len <= 200 + "…".len); // cli counts BYTES
    try testing.expect(std.mem.indexOf(u8, out, "://") == null); // no URL
    var run: usize = 0; // no 24+ token-shaped run
    for (out) |c| {
        run = if (isTokenChar(c)) run + 1 else 0;
        try testing.expect(run < 24);
    }
}

test "sanitizer corpus: cases.json against the cli sanitizer (byte-counted caps)" {
    var w = fx.Walk.start();
    defer w.stop();
    try w.loadOverrides();
    for (try w.cases("llm/fixtures/sanitizer/cases.json")) |case| {
        const name = fx.memberStr(case, "name").?;
        const input_v = fx.member(case, "input").?;
        if (input_v == .null) {
            w.skipped += 1; // Zig has no null string; the cli never calls sanitize on one
            continue;
        }
        w.walked += 1;
        var buf: DetailBuf = undefined;
        const got = llm.sanitizeDetail(input_v.string, &buf);
        try checkInvariants(got);

        // DIVERGENCE(...) cases: the browser literal does not bind non-browser walkers — the invariants above
        // are the contract, and the exact cli text is pinned via the override when one is recorded.
        const divergence = std.mem.startsWith(u8, name, "DIVERGENCE(");
        var want: ?[]const u8 = if (divergence) null else fx.memberStr(case, "expect").?;
        if (w.override("sanitizer", name)) |ov| {
            want = fx.memberStr(ov, "cliExpect") orelse want;
        }
        if (want) |want_text| {
            if (!std.mem.eql(u8, got, want_text))
                w.fail("sanitizer '{s}':\n  want: {s}\n  cli:  {s}\n", .{ name, want_text, got });
        }
    }
    try w.report("sanitizer"); // skipped = the null-input case Zig cannot express
}

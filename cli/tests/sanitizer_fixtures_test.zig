// Walks the shared provider-error sanitizer corpus (llm/fixtures/sanitizer/cases.json)
// against the cli's real sanitizer (llm.sanitizeDetail — the one errorDetail/finish use).
// Expectations in the corpus are the BROWSER's output; per the schema, DIVERGENCE(...)
// cases (and any measured cli drift, pinned in fixture_overrides.json) are recomputed
// locally: the walker then enforces the invariants instead — no URL, no 24+ token run,
// output within the cli's byte-counted cap (200 bytes + ellipsis).
const std = @import("std");
const llm = @import("../src/llm.zig");
const fx = @import("fixture_corpus.zig");
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
    var arena = std.heap.ArenaAllocator.init(testing.allocator);
    defer arena.deinit();
    const a = arena.allocator();
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();

    const overrides = try fx.parseOverrides(a);
    const cases = try fx.loadJson(a, io, "llm/fixtures/sanitizer/cases.json");
    var walked: usize = 0;
    var skipped: usize = 0;
    var failures: usize = 0;

    for (cases.array.items) |case| {
        const name = fx.memberStr(case, "name").?;
        const input_v = fx.member(case, "input").?;
        if (input_v == .null) {
            skipped += 1; // Zig has no null string; the cli never calls sanitize on one
            continue;
        }
        walked += 1;
        var buf: DetailBuf = undefined;
        const got = llm.sanitizeDetail(input_v.string, &buf);
        try checkInvariants(got);

        // DIVERGENCE(...) cases: the browser literal doesn't bind non-browser walkers —
        // the invariants above are the contract; the exact cli text is pinned via the
        // override when one is recorded.
        const divergence = std.mem.startsWith(u8, name, "DIVERGENCE(");
        var want: ?[]const u8 = if (divergence) null else fx.memberStr(case, "expect").?;
        if (fx.overrideFor(overrides, "sanitizer", name)) |ov| {
            want = fx.memberStr(ov, "cliExpect") orelse want;
        }
        if (want) |w| {
            if (!std.mem.eql(u8, got, w)) {
                std.debug.print("sanitizer '{s}':\n  want: {s}\n  cli:  {s}\n", .{ name, w, got });
                failures += 1;
            }
        }
    }
    std.debug.print("sanitizer corpus: walked {d}, skipped {d} (null input)\n", .{ walked, skipped });
    try testing.expectEqual(@as(usize, 0), failures);
}

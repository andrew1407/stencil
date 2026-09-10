//! Cross-language drift guard: PAGE_SIZES in the canonical browser config
//! (browser/js/config/constants.json, embedded at build time) must match the C++
//! core's hard-coded table (core/page/pageMetrics.cpp) name for name and cm for cm.
//! The browser only ever checks that JSON against the wasm build, so without this
//! the CLI's native core could drift from the canon unnoticed.
const std = @import("std");
const core = @import("../src/core.zig");

const constants_json = @embedFile("constants.json");

/// A JSON number as cm — the canon spells whole values as integers ("width": 42).
fn cm(v: std.json.Value) f64 {
    return switch (v) {
        .integer => |i| @floatFromInt(i),
        .float => |f| f,
        else => std.math.nan(f64),
    };
}

test "constants.json PAGE_SIZES matches the core's native page table, both ways" {
    const a = std.testing.allocator;
    const parsed = try std.json.parseFromSlice(std.json.Value, a, constants_json, .{});
    defer parsed.deinit();
    const sizes = parsed.value.object.get("PAGE_SIZES").?.object;
    try std.testing.expect(sizes.count() > 0);

    // core -> JSON: pageFormats() lists exactly the canon's names.
    var listed: usize = 0;
    var names = std.mem.tokenizeScalar(u8, core.pageFormats(), ' ');
    while (names.next()) |name| : (listed += 1) {
        if (sizes.get(name) == null) {
            std.debug.print("core lists page format \"{s}\", the canon does not\n", .{name});
            return error.FormatMissingInCanon;
        }
    }
    try std.testing.expectEqual(sizes.count(), listed);

    // JSON -> core: every canon entry resolves to the same cm dimensions.
    var it = sizes.iterator();
    while (it.next()) |entry| {
        const name = entry.key_ptr.*;
        const o = entry.value_ptr.*.object;
        const page = core.namedPageSize(a, name) orelse {
            std.debug.print("core does not know page format \"{s}\"\n", .{name});
            return error.FormatMissingInCore;
        };
        std.testing.expectEqual(cm(o.get("width").?), page.w) catch |err| {
            std.debug.print("width drift on \"{s}\"\n", .{name});
            return err;
        };
        std.testing.expectEqual(cm(o.get("height").?), page.h) catch |err| {
            std.debug.print("height drift on \"{s}\"\n", .{name});
            return err;
        };
    }
}

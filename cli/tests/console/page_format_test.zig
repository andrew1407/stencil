//! `/format` and `/blank` through console.handle: the picked page drives the layout and a bare
//! blank, survives explicit dims, and only a real edit marks a synced session dirty.
const std = @import("std");
const console = @import("../../src/console.zig");
const image = @import("../../src/media/image.zig");
const cur = @import("console_harness.zig").cur;
const testing = std.testing;

test "console: /format picks the page format that drives the layout and /blank" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();

    const out = "stencil_console_format.json";
    defer dir.deleteFile(io, out) catch {};

    var session = console.Session{ .gpa = a };
    defer session.deinit();

    // A bare /format just lists the formats — nothing picked, session keeps running.
    _ = try console.handle(&session, io, "/format");
    try testing.expectEqual(@as(usize, 0), session.page_size.len);

    // A case-insensitive name is stored canonical.
    _ = try console.handle(&session, io, "/format b5");
    try testing.expectEqualStrings("B5", session.page_size);

    // An unknown name errors and leaves the pick intact.
    _ = try console.handle(&session, io, "/format nope");
    try testing.expectEqualStrings("B5", session.page_size);

    // A bare /blank defaults to the picked format: B5 (17.6×25cm) @ 96dpi -> 665x945 px,
    // and the pick survives the load (the blank was created on that page).
    _ = try console.handle(&session, io, "/blank");
    try testing.expectEqual(@as(usize, 665), cur(&session).width);
    try testing.expectEqual(@as(usize, 945), cur(&session).height);
    try testing.expectEqualStrings("B5", session.page_size);

    // The picked format rides the exported layout as `pageSize`.
    _ = try console.handle(&session, io, "/layout " ++ out);
    const bytes = try dir.readFileAlloc(io, out, a, .limited(1 << 20));
    defer a.free(bytes);
    var parsed = try std.json.parseFromSlice(std.json.Value, a, bytes, .{});
    defer parsed.deinit();
    try testing.expectEqualStrings("B5", parsed.value.object.get("pageSize").?.string);

    // /format custom <w> <h> sets explicit cm dims.
    _ = try console.handle(&session, io, "/format custom 10 15");
    try testing.expectEqualStrings("custom", session.page_size);
    try testing.expectEqual(@as(f64, 10), session.custom_page_w);
    try testing.expectEqual(@as(f64, 15), session.custom_page_h);

    // A bare /blank on a custom pick uses the custom dims (10×15cm @ 96dpi → 378x567 px), keeps the pick,
    // and the page label reflects the custom page actually used — not an A4 fallback.
    _ = try console.handle(&session, io, "/blank");
    try testing.expectEqual(@as(usize, 378), cur(&session).width);
    try testing.expectEqual(@as(usize, 567), cur(&session).height);
    try testing.expectEqualStrings("custom", session.page_size);
    const label = try session.pageFormatLabel();
    defer a.free(label);
    try testing.expectEqualStrings("custom 10×15cm", label);

    // /save routes through that same label (writeOutputLabeled) — the custom page never
    // trips the write, and the file lands with the blank's pixel dims.
    const png_out = "stencil_console_format_custom.png";
    defer dir.deleteFile(io, png_out) catch {};
    _ = try console.handle(&session, io, "/save " ++ png_out);
    const saved = try dir.readFileAlloc(io, png_out, a, .limited(1 << 20));
    defer a.free(saved);
    var simg = try image.decode(a, saved);
    defer simg.deinit(a);
    try testing.expectEqual(@as(usize, 378), simg.width);
    try testing.expectEqual(@as(usize, 567), simg.height);
}

test "console: only a real /format //formula /transform edit marks the session dirty" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var session = console.Session{ .gpa = a };
    defer session.deinit();

    // Pretend a fetched server project is active with sync on — markDirty is gated on both — without
    // touching the network: the flag only queues the debounced upload, and nothing here flushes it.
    session.sync = true;
    try session.setRemote("http://127.0.0.1:9", "proj-1");

    _ = try console.handle(&session, io, "/blank 64 48 white");
    session.dirty = false; // probe from a clean slate

    // Pure listings and rejected arguments mutate nothing → never dirty (a spurious dirty
    // would re-upload the UNCHANGED project, bumping its changed-time for every peer).
    _ = try console.handle(&session, io, "/format"); // bare = list only
    try testing.expect(!session.dirty);
    _ = try console.handle(&session, io, "/format nope"); // unknown name = error only
    try testing.expect(!session.dirty);
    _ = try console.handle(&session, io, "/formula"); // bare = show only
    try testing.expect(!session.dirty);
    _ = try console.handle(&session, io, "/formula x foo(x)"); // invalid expression
    try testing.expect(!session.dirty);
    _ = try console.handle(&session, io, "/crop"); // bare transform = usage only
    try testing.expect(!session.dirty);
    _ = try console.handle(&session, io, "/rotate 4"); // full turn = explicit no-op
    try testing.expect(!session.dirty);
    _ = try console.handle(&session, io, "/exec"); // bare = usage only
    try testing.expect(!session.dirty);
    _ = try console.handle(&session, io, "/filter frobnicate"); // unknown filter
    try testing.expect(!session.dirty);

    // A successful pick / expression / edit does queue the sync upload.
    _ = try console.handle(&session, io, "/format b5");
    try testing.expect(session.dirty);
    session.dirty = false;
    _ = try console.handle(&session, io, "/formula x x*2");
    try testing.expect(session.dirty);
    session.dirty = false;
    _ = try console.handle(&session, io, "/rotate 1");
    try testing.expect(session.dirty);
}

test "console: /blank with explicit dims keeps the /format pick (bot parity)" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();

    const out = "stencil_console_blank_dims.json";
    defer dir.deleteFile(io, out) catch {};

    var session = console.Session{ .gpa = a };
    defer session.deinit();

    // Explicit dims size the blank but do not drop the picked format — the same sequence in
    // the Telegram bot keeps PageFormat=B5 — so the layout written on save/sync carries it.
    _ = try console.handle(&session, io, "/format b5");
    _ = try console.handle(&session, io, "/blank 800 600 white");
    try testing.expectEqual(@as(usize, 800), cur(&session).width);
    try testing.expectEqual(@as(usize, 600), cur(&session).height);
    try testing.expectEqualStrings("B5", session.page_size);

    // The page label (header + /save wrote line) reports B5 oriented to the landscape image.
    const label = try session.pageFormatLabel();
    defer a.free(label);
    try testing.expectEqualStrings("B5 25×17.6cm", label);

    _ = try console.handle(&session, io, "/layout " ++ out);
    const bytes = try dir.readFileAlloc(io, out, a, .limited(1 << 20));
    defer a.free(bytes);
    var parsed = try std.json.parseFromSlice(std.json.Value, a, bytes, .{});
    defer parsed.deinit();
    try testing.expectEqualStrings("B5", parsed.value.object.get("pageSize").?.string);

    // Same for a custom pick: the format and its cm dims survive an explicit-dims blank.
    _ = try console.handle(&session, io, "/format custom 10 15");
    _ = try console.handle(&session, io, "/blank 64 48 red");
    try testing.expectEqualStrings("custom", session.page_size);
    try testing.expectEqual(@as(f64, 10), session.custom_page_w);
    try testing.expectEqual(@as(f64, 15), session.custom_page_h);
}

test "console: /blank takes a leading page-format token" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var session = console.Session{ .gpa = a };
    defer session.deinit();

    // A6 is 10.5×14.8cm -> 397x559 px @ 96dpi; the token also becomes the session's pick.
    _ = try console.handle(&session, io, "/blank a6 red");
    try testing.expectEqual(@as(usize, 397), cur(&session).width);
    try testing.expectEqual(@as(usize, 559), cur(&session).height);
    try testing.expectEqualStrings("A6", session.page_size);

    // A format token and explicit dims are mutually exclusive → error, image unchanged.
    _ = try console.handle(&session, io, "/blank b5 800 600");
    try testing.expectEqual(@as(usize, 397), cur(&session).width);
}

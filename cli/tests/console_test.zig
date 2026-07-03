// Integration: drive console mode's command handler end-to-end. Writes the PNG fixture to
// disk, then runs upload / crop / rotate / filter / undo / redo / reset / save commands
// through console.handle (the same path the interactive --console loop uses) and checks the
// working image, its undo history, and the saved output.
const std = @import("std");
const console = @import("../src/console.zig");
const image = @import("../src/image.zig");
const testing = std.testing;
const sample = @embedFile("fixtures/sample.png");

fn cur(session: *console.Session) image.Rgba8 {
    return session.current().*;
}

test "console: upload -> crop -> rotate, with undo / redo / reset / save" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();

    const in = "stencil_console_in.png";
    const out = "stencil_console_out.png";
    try dir.writeFile(io, .{ .sub_path = in, .data = sample });
    defer dir.deleteFile(io, in) catch {};
    defer dir.deleteFile(io, out) catch {};

    var session = console.Session{ .gpa = a };
    defer session.deinit();

    // A transform before a source is loaded is a no-op (no image), not a crash.
    try testing.expect(!try console.handle(&session, io, "/filter sepia"));
    try testing.expectEqual(@as(usize, 0), session.stateCount());

    try testing.expect(!try console.handle(&session, io, "/upload " ++ in));
    try testing.expectEqual(@as(usize, 1), session.stateCount());
    try testing.expectEqual(@as(usize, 16), cur(&session).width);
    try testing.expectEqual(@as(usize, 12), cur(&session).height);
    try testing.expect(!session.temp); // a local file is not a temporary in-memory source

    _ = try console.handle(&session, io, "/crop x1=0% x2=50% y1=0% y2=100%");
    try testing.expectEqual(@as(usize, 8), cur(&session).width);

    _ = try console.handle(&session, io, "/rotate 1");
    try testing.expectEqual(@as(usize, 12), cur(&session).width);
    try testing.expectEqual(@as(usize, 8), cur(&session).height);

    // Undo the rotate, then the crop: back to the 16x12 original.
    _ = try console.handle(&session, io, "/undo");
    try testing.expectEqual(@as(usize, 8), cur(&session).width);
    try testing.expectEqual(@as(usize, 12), cur(&session).height);
    _ = try console.handle(&session, io, "/undo");
    try testing.expectEqual(@as(usize, 16), cur(&session).width);
    try testing.expectEqual(@as(usize, 0), session.cursor);

    // Redo the crop.
    _ = try console.handle(&session, io, "/redo");
    try testing.expectEqual(@as(usize, 8), cur(&session).width);

    // A fresh edit from here drops the (rotate) redo state and becomes the latest.
    _ = try console.handle(&session, io, "/filter sepia");
    try testing.expectEqual(session.stateCount(), session.cursor + 1);

    _ = try console.handle(&session, io, "/save " ++ out);
    const bytes = try dir.readFileAlloc(io, out, a, .limited(1 << 20));
    defer a.free(bytes);
    var img = try image.decode(a, bytes);
    defer img.deinit(a);
    try testing.expectEqual(@as(usize, 8), img.width);
    try testing.expectEqual(@as(usize, 12), img.height);

    // Reset returns to the original and clears history.
    _ = try console.handle(&session, io, "/reset");
    try testing.expectEqual(@as(usize, 1), session.stateCount());
    try testing.expectEqual(@as(usize, 16), cur(&session).width);

    // Drop forgets the image entirely.
    _ = try console.handle(&session, io, "/drop");
    try testing.expectEqual(@as(usize, 0), session.stateCount());

    try testing.expect(try console.handle(&session, io, "/exit")); // exit ends the session
}

test "console: /layout exports the structured layout JSON to a file" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();

    const layout_in = "stencil_console_layout_in.json";
    const out = "stencil_console_layout.json";
    try dir.writeFile(io, .{
        .sub_path = layout_in,
        .data =
        \\{"lines":[{"points":[{"x":1,"y":2},{"x":3,"y":4}],"color":"#ff0000","width":2}]}
        ,
    });
    defer dir.deleteFile(io, layout_in) catch {};
    defer dir.deleteFile(io, out) catch {};

    var session = console.Session{ .gpa = a };
    defer session.deinit();

    // A blank page + a drawn layout, then export the layout JSON.
    _ = try console.handle(&session, io, "/blank 64 48 white");
    _ = try console.handle(&session, io, "/apply " ++ layout_in);
    _ = try console.handle(&session, io, "/layout " ++ out);

    const bytes = try dir.readFileAlloc(io, out, a, .limited(1 << 20));
    defer a.free(bytes);

    // It parses as JSON and carries the structured "lines" array.
    var parsed = try std.json.parseFromSlice(std.json.Value, a, bytes, .{});
    defer parsed.deinit();
    try testing.expect(parsed.value == .object);
    try testing.expect(parsed.value.object.get("lines") != null);
    try testing.expect(parsed.value.object.get("lines").? == .array);
    try testing.expect(parsed.value.object.get("lines").?.array.items.len >= 1);
}

test "console: /formula sets validated formulas that ride the exported layout" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();

    const out = "stencil_console_formula.json";
    defer dir.deleteFile(io, out) catch {};

    var session = console.Session{ .gpa = a };
    defer session.deinit();

    _ = try console.handle(&session, io, "/blank 64 48 white");
    _ = try console.handle(&session, io, "/formula x x*2 + 1");
    _ = try console.handle(&session, io, "/formula y y/3");
    // An invalid expression is rejected and leaves the prior value intact.
    _ = try console.handle(&session, io, "/formula x foo(x)");
    try testing.expectEqualStrings("x*2 + 1", session.formula_x);
    try testing.expectEqualStrings("y/3", session.formula_y);
    try testing.expect(session.allow_formulas);

    _ = try console.handle(&session, io, "/layout " ++ out);
    const bytes = try dir.readFileAlloc(io, out, a, .limited(1 << 20));
    defer a.free(bytes);
    var parsed = try std.json.parseFromSlice(std.json.Value, a, bytes, .{});
    defer parsed.deinit();
    const obj = parsed.value.object;
    try testing.expectEqualStrings("x*2 + 1", obj.get("formulaX").?.string);
    try testing.expectEqualStrings("y/3", obj.get("formulaY").?.string);
    try testing.expect(obj.get("allowFormulas").?.bool);
}

test "console: /project-color without an active server project is a graceful no-op" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var session = console.Session{ .gpa = a };
    defer session.deinit();

    // No connection / no fetched project: both the show and set forms report an error and
    // return false (the session keeps running), never touching the network or crashing.
    try testing.expect(!try console.handle(&session, io, "/project-color"));
    try testing.expect(!try console.handle(&session, io, "/project-color #ff5623"));
    try testing.expect(!session.hasRemote());
}

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

    // A bare /blank on a custom pick uses the custom dims (10×15cm @ 96dpi -> 378x567 px),
    // keeps the pick, and the page label (header + /save wrote line) reflects the custom
    // page actually used — not an A4 fallback.
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

    // Pretend a fetched server project is active with sync on — markDirty is gated on both —
    // without touching the network: the flag only queues the debounced upload, and nothing
    // here flushes it.
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

test "console: bare transforms list usage/variants instead of acting" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var session = console.Session{ .gpa = a };
    defer session.deinit();

    _ = try console.handle(&session, io, "/blank 64 48 white");
    try testing.expectEqual(@as(usize, 1), session.stateCount());

    // A bare /crop no longer records a silent full-image crop — usage only, no new state.
    _ = try console.handle(&session, io, "/crop");
    try testing.expectEqual(@as(usize, 1), session.stateCount());
    try testing.expectEqual(@as(usize, 64), cur(&session).width);

    // A bare /rotate, /filter and /exec likewise only print variants.
    _ = try console.handle(&session, io, "/rotate");
    _ = try console.handle(&session, io, "/filter");
    _ = try console.handle(&session, io, "/exec");
    try testing.expectEqual(@as(usize, 1), session.stateCount());
}

test "console: invert and contour filters" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var session = console.Session{ .gpa = a };
    defer session.deinit();

    _ = try console.handle(&session, io, "/blank 4 4 white");

    // Invert flips white to black (via the /invert shorthand); the mode is structured state.
    _ = try console.handle(&session, io, "/invert");
    try testing.expectEqualStrings("invert", session.state().filter_mode);
    try testing.expectEqual(@as(u8, 0), cur(&session).pixels[0]);
    try testing.expectEqual(@as(u8, 255), cur(&session).pixels[3]); // alpha preserved

    // Contour on a flat page finds no edges — uniform white again.
    _ = try console.handle(&session, io, "/filter contour");
    try testing.expectEqualStrings("contour", session.state().filter_mode);
    try testing.expectEqual(@as(u8, 255), cur(&session).pixels[0]);

    // An unknown mode still errors without adding a state.
    const n = session.stateCount();
    _ = try console.handle(&session, io, "/filter frobnicate");
    try testing.expectEqual(n, session.stateCount());
}

test "console: blank creates a temporary in-memory source" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var session = console.Session{ .gpa = a };
    defer session.deinit();

    _ = try console.handle(&session, io, "/blank 64 48 red");
    try testing.expectEqual(@as(usize, 1), session.stateCount());
    try testing.expect(session.temp);
    try testing.expectEqual(@as(usize, 64), cur(&session).width);
    try testing.expectEqual(@as(usize, 48), cur(&session).height);
    try testing.expectEqualStrings("blank", session.label.?);
}

// Integration: drive console mode's command handler end-to-end. Writes the PNG fixture to
// disk, then runs upload / crop / rotate / filter / undo / redo / reset / save commands
// through console.handle (the same path the interactive --console loop uses) and checks the
// working image, its undo history, and the saved output.
const std = @import("std");
const console = @import("../../src/console.zig");
const image = @import("../../src/media/image.zig");
const logo = @import("../../src/app/logo.zig");
const harness = @import("console_harness.zig");
const Capture = harness.Capture;
const cur = harness.cur;
const sample = harness.sample;
const testing = std.testing;

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

test "console: the secret words are listed only by /eastereggs, and need the full screen to go on" {
    const skin = @import("../../src/app/skin.zig");
    const commands = @import("../../src/console/commands.zig");
    const msg = @import("../../src/app/messages.zig");
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var session = console.Session{ .gpa = a };
    defer session.deinit();
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();

    try testing.expect(!try console.handle(&session, io, "/eastereggs"));
    for (std.enums.values(skin.Skin)) |s| {
        const word = skin.traitsOf(s).word;
        if (word.len == 0) continue;
        try testing.expect(std.mem.indexOf(u8, cap.text(), word) != null);
        try testing.expect(commands.verbOf(word) == null); // no Verb, so neither /help nor Tab can name it
    }
    cap.buf.clearRetainingCapacity();
    // Outside the full-screen console a secret only says where it works; nothing goes on.
    try testing.expect(!try console.handle(&session, io, "/meow"));
    try testing.expectEqualStrings(msg.eggs_full_screen_only, cap.text());
    try testing.expectEqual(skin.Skin.none, skin.get());
}

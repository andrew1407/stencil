//! `/images` and `/unpaste`: what the turn will send, and taking an attachment back — the newest,
//! a numbered one, or the working image once the turn's attachments are spent.
const std = @import("std");
const image = @import("../../src/media/image.zig");
const logo = @import("../../src/app/logo.zig");
const core = @import("../../src/core.zig");
const Session = @import("../../src/console/session.zig").Session;
const attachments = @import("../../src/console/attachments.zig");
const doImages = attachments.doImages;
const doUnpaste = attachments.doUnpaste;
const llmPrompt = @import("../../src/console/llmPrompt.zig");
const testing = std.testing;

/// Add a w×h picture the way `/upload` and `/paste` do: load it as the working image AND
/// attach its encoded bytes to the turn. Ownership of both moves into the session.
fn addPicture(session: *Session, w: usize, h: usize, label: []const u8) !void {
    const gpa = session.gpa;
    const px = try gpa.alloc(u8, w * h * 4);
    core.fillRGBA(px, @intCast(w * h), .{ .r = 10, .g = 20, .b = 30, .a = 255 });
    const img = image.Rgba8{ .width = w, .height = h, .pixels = px };
    const bytes = try image.encode(gpa, img, .png);
    errdefer gpa.free(bytes);
    try session.loadImage(img, label, true, .png, null);
    try session.addAttachment(label, bytes, .png, true);
}

test "images: lists what the turn will send, and /unpaste <n> drops one of them" {
    const a = testing.allocator;
    var cap = llmPrompt.Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = Session{ .gpa = a };
    defer session.deinit();

    try addPicture(&session, 4, 4, "first.png");
    try addPicture(&session, 6, 2, "second.png");
    doImages(&session);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "2 image(s) on this turn") != null);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "1. first.png") != null);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "2. second.png") != null);
    // The working image is the one just loaded — marked so the numbering is unambiguous.
    try testing.expect(std.mem.indexOf(u8, cap.text(), "the working image") != null);

    // A specific one can go, not just the newest.
    doUnpaste(&session, "1");
    try testing.expectEqual(@as(usize, 1), session.liveAttachments().len);
    try testing.expectEqualStrings("second.png", session.liveAttachments()[0].label);
    // An index the turn cannot satisfy says so instead of removing anything.
    doUnpaste(&session, "7");
    try testing.expect(std.mem.indexOf(u8, cap.text(), "no attached image 7") != null);
    try testing.expectEqual(@as(usize, 1), session.liveAttachments().len);
}

test "unpaste: takes back the newest attachment, restoring the one before it" {
    const a = testing.allocator;
    var quiet: u8 = 0;
    logo.setSink(llmPrompt.swallowPrint, &quiet);
    defer logo.clearSink();
    var session = Session{ .gpa = a };
    defer session.deinit();

    try addPicture(&session, 4, 4, "first");
    try addPicture(&session, 6, 2, "second");
    try testing.expectEqual(@as(usize, 6), session.current().width);

    doUnpaste(&session, "");

    // The picture before it is the working image again, and only it is still attached.
    try testing.expectEqual(@as(usize, 4), session.current().width);
    try testing.expectEqual(@as(usize, 1), session.attachments.items.len);
    try testing.expectEqualStrings("first", session.label.?);

    // With nothing left to take back, the working image itself goes.
    doUnpaste(&session, "");
    try testing.expect(!session.hasImage());
    try testing.expectEqual(@as(usize, 0), session.attachments.items.len);
}

test "unpaste: a spent turn's attachments stay put — the working image goes instead" {
    const a = testing.allocator;
    var quiet: u8 = 0;
    logo.setSink(llmPrompt.swallowPrint, &quiet);
    defer logo.clearSink();
    var session = Session{ .gpa = a };
    defer session.deinit();

    try addPicture(&session, 4, 4, "sent");
    session.consumeAttachments(); // a /prompt used them: that turn is over

    doUnpaste(&session, "");

    try testing.expect(!session.hasImage());
    try testing.expectEqual(@as(usize, 1), session.attachments.items.len);
}

test "unpaste with nothing loaded says so instead of erroring" {
    const a = testing.allocator;
    var cap = llmPrompt.Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = Session{ .gpa = a };
    defer session.deinit();

    doUnpaste(&session, "");

    try testing.expect(std.mem.indexOf(u8, cap.text(), "nothing to remove") != null);
}

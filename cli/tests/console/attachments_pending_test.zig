//! Images pasted into the line being typed: a submitted line's pending pictures become the
//! turn's uploads, a pasted path is claimed only where a picture fits, and each line's cap.
const std = @import("std");
const logo = @import("../../src/app/logo.zig");
const Session = @import("../../src/console/session.zig").Session;
const attachments = @import("../../src/console/attachments.zig");
const drainPending = attachments.drainPending;
const pending = @import("../../src/console/attachments/pending.zig");
const pathPasteAllowed = pending.pathPasteAllowed;
const roomForPending = pending.roomForPending;
const pathPasteLimit = pending.pathPasteLimit;
const max_paste_path = pending.max_paste_path;
const unquotePath = pending.unquotePath;
const shortLabel = pending.shortLabel;
const llmPrompt = @import("../../src/console/llmPrompt.zig");
const testing = std.testing;

test "a submitted line's pasted images become this turn's uploads" {
    const a = testing.allocator;
    var quiet: u8 = 0;
    logo.setSink(llmPrompt.swallowPrint, &quiet);
    defer logo.clearSink();
    var session = Session{ .gpa = a };
    defer session.deinit();

    try session.addPending("clipboard", try llmPrompt.pngOf(a, 4, 4), .png, true);
    try session.addPending("shot.png", try llmPrompt.pngOf(a, 6, 2), .png, false);

    // A line that is nothing but the pictures IS the upload — nothing else runs after it.
    try testing.expect(drainPending(&session, ""));
    try testing.expectEqual(@as(usize, 0), session.pending.items.len);
    try testing.expectEqual(@as(usize, 2), session.liveAttachments().len);
    // The last one pasted is the working image, and both ride the next /prompt (§2.1).
    try testing.expectEqual(@as(usize, 6), session.current().width);
    try testing.expectEqualStrings("shot.png", session.label.?);
    try testing.expect(!session.temp); // a file-backed paste is not an in-memory source
}

test "a command on the same line still runs; a bare /upload is served by the paste itself" {
    const a = testing.allocator;
    var quiet: u8 = 0;
    logo.setSink(llmPrompt.swallowPrint, &quiet);
    defer logo.clearSink();
    var session = Session{ .gpa = a };
    defer session.deinit();

    try session.addPending("clipboard", try llmPrompt.pngOf(a, 4, 4), .png, true);
    try testing.expect(!drainPending(&session, "/prompt what is this?")); // the prompt still has to run
    try testing.expectEqual(@as(usize, 1), session.liveAttachments().len);

    try session.addPending("clipboard", try llmPrompt.pngOf(a, 8, 8), .png, true);
    try testing.expect(drainPending(&session, "/upload")); // a bare upload wants exactly this
    try testing.expectEqual(@as(usize, 8), session.current().width);

    // Nothing pending: an ordinary line is left entirely alone.
    try testing.expect(!drainPending(&session, "/rotate 1"));
}

test "keepPending drops the images whose markers the user deleted, in the new order" {
    const a = testing.allocator;
    var session = Session{ .gpa = a };
    defer session.deinit();

    try session.addPending("one.png", try llmPrompt.pngOf(a, 4, 4), .png, false);
    try session.addPending("two.png", try llmPrompt.pngOf(a, 6, 2), .png, false);
    try session.addPending("three.png", try llmPrompt.pngOf(a, 8, 8), .png, false);

    session.keepPending(&.{ 2, 0 }); // the middle marker was deleted, and the order changed
    try testing.expectEqual(@as(usize, 2), session.pending.items.len);
    try testing.expectEqualStrings("three.png", session.pending.items[0].label);
    try testing.expectEqualStrings("one.png", session.pending.items[1].label);

    session.clearPending(); // an abandoned line frees the rest
    try testing.expectEqual(@as(usize, 0), session.pending.items.len);
}

test "a pasted path is taken as a picture only when it names one, under a verb that takes one" {
    // Only the two verbs that load images (and a line with no command yet) claim a path;
    // pasted into anything else it is still just text.
    try testing.expect(pathPasteAllowed(""));
    try testing.expect(pathPasteAllowed("/prompt describe "));
    try testing.expect(pathPasteAllowed("/upload "));
    try testing.expect(pathPasteAllowed("/uplo")); // half-typed: not a command yet
    try testing.expect(!pathPasteAllowed("/save "));
    try testing.expect(!pathPasteAllowed("/rename "));

    var buf: [max_paste_path]u8 = undefined;
    try testing.expectEqualStrings("/tmp/a.png", unquotePath(&buf, "  /tmp/a.png ").?);
    try testing.expectEqualStrings("/tmp/my pic.png", unquotePath(&buf, "'/tmp/my pic.png'").?);
    try testing.expectEqualStrings("/tmp/my pic.png", unquotePath(&buf, "/tmp/my\\ pic.png").?);
    try testing.expectEqual(@as(?[]const u8, null), unquotePath(&buf, "look at /tmp/a.png")); // prose
    try testing.expectEqual(@as(?[]const u8, null), unquotePath(&buf, "x"));

    // The marker's label is the file name, elided in the middle so the extension survives.
    var lb: [20]u8 = undefined;
    try testing.expectEqualStrings("photo.png", shortLabel(&lb, "/home/me/pics/photo.png"));
    try testing.expectEqualStrings("a_file_.png", shortLabel(&lb, "a[file].png")); // never breaks the marker
    try testing.expectEqualStrings("aaaaaaaaa…name.png", shortLabel(&lb, "aaaaaaaaabbbbbbbbbcccc-name.png"));
}

test "an upload line takes ONE image; a prompt line takes three" {
    const a = testing.allocator;
    var cap = llmPrompt.Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = Session{ .gpa = a };
    defer session.deinit();

    // Nothing held yet: any line has room.
    try testing.expect(roomForPending(&session, ""));
    try testing.expect(roomForPending(&session, "/upload "));

    try session.addPending("clipboard", try llmPrompt.pngOf(a, 4, 4), .png, true);
    // A second picture would quietly change what Enter does on an upload line.
    try testing.expect(!roomForPending(&session, "/upload"));
    try testing.expect(!roomForPending(&session, "/open "));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "'/upload' takes one image") != null);
    // A prompt (or a line with no command word yet) keeps going, up to the third.
    try testing.expect(roomForPending(&session, "/prompt compare "));
    try testing.expect(roomForPending(&session, ""));

    try session.addPending("clipboard", try llmPrompt.pngOf(a, 4, 4), .png, true);
    try session.addPending("clipboard", try llmPrompt.pngOf(a, 4, 4), .png, true);
    try testing.expect(!roomForPending(&session, "/prompt compare "));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "at most 3 images on one line") != null);

    // A pasted PATH obeys the same caps, silently (it just stays text).
    try testing.expectEqual(@as(usize, 1), pathPasteLimit("/upload"));
    try testing.expectEqual(@as(usize, 3), pathPasteLimit("/prompt look"));
    try testing.expectEqual(@as(usize, 3), pathPasteLimit(""));
}

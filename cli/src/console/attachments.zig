//! Clipboard and pending-attachment machinery for the console: /copy, /paste,
//! /unpaste and /images, plus the images pasted INTO the line being typed
//! (Ctrl-V or a pasted path) that become the turn's uploads on submit.
const std = @import("std");
const image = @import("../media/image.zig");
const pipeline = @import("../pipeline.zig");
const logo = @import("../app/logo.zig");
const core = @import("../core.zig");
const clipboard = @import("../clipboard.zig");
const commands = @import("commands.zig");
const line_edit = @import("../line_edit.zig");
const ui = @import("ui.zig");
const Session = @import("session.zig").Session;
const Attachment = @import("session.zig").Attachment;
const handlers = @import("handlers.zig");
const attachments = @import("attachments.zig");
const llmPrompt = @import("llmPrompt.zig");
const clip = @import("attachments/clip.zig");
const pending = @import("attachments/pending.zig");

pub const doCopy = clip.doCopy;
pub const doPaste = clip.doPaste;
pub const clipboardToImage = clip.clipboardToImage;
pub const doUnpaste = clip.doUnpaste;

pub const pasteAtPrompt = pending.pasteAtPrompt;
pub const capturePendingPath = pending.capturePendingPath;
pub const drainPending = pending.drainPending;
const pathPasteAllowed = pending.pathPasteAllowed;
const roomForPending = pending.roomForPending;
const pathPasteLimit = pending.pathPasteLimit;
const max_paste_path = pending.max_paste_path;
const unquotePath = pending.unquotePath;
const holdPending = pending.holdPending;
const shortLabel = pending.shortLabel;

/// `/images` — what the next `/prompt` will send along: every image added this turn, in the
/// order an `{"op":"image","index":N}` action indexes them, with the working one marked.
pub fn doImages(session: *Session) void {
    const items = session.liveAttachments();
    if (items.len == 0) {
        logo.print("no images attached to this turn — /upload, /paste or Ctrl-V adds one\n", .{});
        return;
    }
    logo.print("{d} image(s) on this turn — /prompt sends them all, /unpaste <n> drops one:\n", .{items.len});
    for (items, 1..) |att, i| {
        const current = session.label != null and std.mem.eql(u8, session.label.?, att.label);
        logo.print("  {d}. {s} ({d} KB){s}\n", .{
            i,
            att.label,
            (att.bytes.len + 1023) / 1024,
            if (current) " — the working image" else "",
        });
    }
}

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

const testing = std.testing;

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

test {
    _ = clip;
    _ = pending;
}

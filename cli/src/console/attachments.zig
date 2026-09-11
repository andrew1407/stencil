//! Clipboard and pending-attachment machinery for the console: /copy, /paste,
//! /unpaste and /images, plus the images pasted INTO the line being typed
//! (Ctrl-V or a pasted path) that become the turn's uploads on submit.
const std = @import("std");
const image = @import("../image.zig");
const pipeline = @import("../pipeline.zig");
const logo = @import("../logo.zig");
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

pub fn doCopy(session: *Session, io: std.Io) !void {
    if (!session.hasImage()) return ui.noImage();
    const img = session.current().*;
    const png = image.encode(session.gpa, img, .png) catch |e| {
        logo.err("could not encode the image for the clipboard ({s})\n", .{@errorName(e)});
        return;
    };
    defer session.gpa.free(png);
    clipboard.writeImage(session.gpa, io, png) catch |e| return clipError("copy", e);
    logo.print("copied to clipboard ({d}x{d})\n", .{ img.width, img.height });
}

pub fn doPaste(session: *Session, io: std.Io) !void {
    _ = try clipboardToImage(session, io, false);
}

/// Load the clipboard's image as the working image AND a turn attachment, exactly as an
/// `/upload` does. `quiet` swallows the "there was nothing to take" outcomes so a bare
/// `/upload` can fall back to its usage line; a real failure always prints.
pub fn clipboardToImage(session: *Session, io: std.Io, quiet: bool) !bool {
    const bytes = clipboard.readImage(session.gpa, io) catch |e| {
        if (!quiet or !isEmptyClipboard(e)) clipError("paste", e);
        return false;
    };
    defer session.gpa.free(bytes);
    const img = image.decode(session.gpa, bytes) catch |e| {
        logo.err("the clipboard image could not be decoded ({s})\n", .{@errorName(e)});
        return false;
    };
    // §2.1: a paste joins the turn's attachments exactly like an /upload, so several pasted
    // pictures all ride the next /prompt (and `/unpaste` has something to take back).
    // Best-effort: a copy we can't afford just isn't one.
    const att_bytes: ?[]u8 = session.gpa.dupe(u8, bytes) catch null;
    errdefer if (att_bytes) |b| session.gpa.free(b);
    try session.loadImage(img, "clipboard", true, .png, null);
    if (att_bytes) |b| session.addAttachment("clipboard", b, .png, true) catch {};
    ui.redraw(session);
    // Say what just landed and how to work with it — a pasted picture you cannot see listed
    // is one you cannot refer to (`image` index N) or take back.
    const n_att = session.liveAttachments().len;
    if (n_att > 1) {
        logo.note("pasted as image {d} of {d} this turn — '/images' lists them, Ctrl-Z removes it\n", .{ n_att, n_att });
    } else {
        logo.note("pasted from the clipboard — Ctrl-Z removes it\n", .{});
    }
    return true;
}

// Clipboard outcomes that mean "nothing to paste" rather than a failure — the cases a bare
// `/upload` treats as "no image was offered" and answers with its usage line.
fn isEmptyClipboard(e: anyerror) bool {
    return e == clipboard.Error.NoImage or e == clipboard.Error.Unsupported or e == clipboard.Error.ToolMissing;
}

/// `/unpaste` (Ctrl-Alt-Z) — take back the last image added this turn: the newest
/// attachment goes and the one before it becomes the working image again; with none left
/// it drops the working image.
pub fn doUnpaste(session: *Session, arg: []const u8) void {
    // `/unpaste 2` takes back a SPECIFIC one (the numbering `/images` prints); bare takes the
    // newest, which is what the Ctrl-Z chord sends.
    const trimmed = std.mem.trim(u8, arg, " \t");
    if (trimmed.len != 0) {
        const n = std.fmt.parseInt(usize, trimmed, 10) catch 0;
        if (n == 0 or n > session.liveAttachments().len) {
            logo.err("no attached image {s} — '/images' lists them\n", .{trimmed});
            return;
        }
        var gone = session.removeAttachment(n - 1) orelse return;
        defer gone.deinit(session.gpa);
        logo.note("removed image {d} \"{s}\" — {d} left this turn\n", .{ n, gone.label, session.liveAttachments().len });
        return;
    }
    if (session.popAttachment()) |att| {
        var gone = att;
        defer gone.deinit(session.gpa);
        if (session.lastAttachment()) |prev| {
            // Reload the previous attachment as the working image. loadImage resets the edit
            // history (a different picture) but leaves the attachment list alone.
            const img = image.decode(session.gpa, prev.bytes) catch |e| {
                logo.err("could not restore \"{s}\" ({s})\n", .{ prev.label, @errorName(e) });
                return;
            };
            const label = prev.label;
            const source = session.gpa.dupe(u8, prev.bytes) catch null;
            session.loadImage(img, label, prev.temp, prev.fmt, source) catch |e| {
                logo.err("could not restore \"{s}\" ({s})\n", .{ label, @errorName(e) });
                return;
            };
            logo.note("removed \"{s}\" — back to \"{s}\"\n", .{ gone.label, label });
        } else {
            session.clearAll();
            logo.note("removed \"{s}\" — no image left\n", .{gone.label});
        }
        ui.redraw(session);
        return;
    }
    if (!session.hasImage()) {
        logo.note("nothing to remove — no working image and no attachments this turn\n", .{});
        return;
    }
    handlers.doDrop(session);
}

fn clipError(verb: []const u8, e: anyerror) void {
    switch (e) {
        clipboard.Error.Unsupported => logo.err("clipboard {s} is only supported on macOS\n", .{verb}),
        clipboard.Error.ToolMissing => logo.err("'osascript' not found — clipboard {s} needs macOS\n", .{verb}),
        clipboard.Error.NoImage => logo.err("no image on the clipboard to paste\n", .{}),
        else => logo.err("clipboard {s} failed ({s})\n", .{ verb, @errorName(e) }),
    }
}

// images pasted INTO the line being typed (Ctrl-V, or a pasted image path)
// The editor owns the `[Image #N <label>]` markers in the line; the session owns the bytes
// behind them. Nothing is loaded until the line is submitted (drainPending) — deleting a
// marker, Ctrl-C or Ctrl-U simply drops what it stood for.

const max_paste_path = 1024; // longest pasted path we consider (well past PATH_MAX in practice)
const max_paste_bytes = 64 << 20; // 64 MiB cap on a pasted image file, matching the clipboard's

/// Ctrl-V while typing: hold the clipboard's picture against the line being edited. Writes
/// the marker's short label into `out` and returns its length; null when there was nothing
/// to take (the reason is printed).
pub fn pasteAtPrompt(session: *Session, io: std.Io, before: []const u8, label: []u8, text: []u8) line_edit.PasteResult {
    const gpa = session.gpa;
    // A picture first — that is the whole point of the chord.
    if (clipboard.readImage(gpa, io)) |bytes| {
        if (!roomForPending(session, before)) { // the line is full: said why
            gpa.free(bytes);
            return .none;
        }
        const n = holdPending(session, bytes, "clipboard", .png, true, label) orelse return .none;
        return .{ .image = n };
    } else |e| if (e != clipboard.Error.NoImage) {
        clipError("paste", e); // unsupported / no tool / a real failure
        return .none;
    }
    // No picture: Ctrl-V is still the paste key, so type what WAS copied.
    const t = clipboard.readText(gpa, io) catch |e| {
        if (e == clipboard.Error.NoImage) {
            logo.note("nothing on the clipboard to paste\n", .{});
        } else {
            clipError("paste", e);
        }
        return .none;
    };
    defer gpa.free(t);
    const n = @min(t.len, text.len);
    @memcpy(text[0..n], t[0..n]);
    return .{ .text = n };
}

/// How many images the line being typed may carry, with a printed reason when full:
/// `/upload` takes exactly ONE, a `/prompt` (or a line with no command word yet) up to
/// three. The verbs live here — the editor knows nothing about commands.
fn roomForPending(session: *Session, before: []const u8) bool {
    const held = session.pending.items.len;
    const cmd = commands.parseCommand(before);
    const verb = commands.verbOf(cmd.word);
    if (verb != null and verb.? == .upload) {
        if (held == 0) return true;
        logo.note("'/upload' takes one image — press Enter to load it, or '/prompt' for up to {d}\n", .{line_edit.max_pending_images});
        return false;
    }
    if (held < line_edit.max_pending_images) return true;
    logo.note("at most {d} images on one line — Backspace over a marker takes one back\n", .{line_edit.max_pending_images});
    return false;
}

/// A ⌘V paste that is nothing but the path of an image FILE (how a terminal pastes a picture
/// dragged out of a file manager — the bytes themselves never reach us). Silent about
/// everything it does not accept: the paste is then ordinary text, which is what it meant.
pub fn capturePendingPath(session: *Session, io: std.Io, text: []const u8, before: []const u8, out: []u8) ?usize {
    if (!pathPasteAllowed(before)) return null;
    if (session.pending.items.len >= pathPasteLimit(before)) return null; // full: the paste stays text
    var pbuf: [max_paste_path]u8 = undefined;
    const path = unquotePath(&pbuf, text) orelse return null;
    const fmt = image.formatFromExt(std.fs.path.extension(path)) orelse return null;
    const gpa = session.gpa;
    const full = pipeline.expandHome(gpa, path) catch return null;
    defer gpa.free(full);
    const bytes = std.Io.Dir.cwd().readFileAlloc(io, full, gpa, .limited(max_paste_bytes)) catch return null;
    return holdPending(session, bytes, path, fmt, false, out);
}

/// Turn the images pasted into a SUBMITTED line into real uploads, each adopted the way
/// `/paste` adopts one (§2.1), in marker order. True when the paste WAS the whole command
/// (a line of only images, or a bare `/upload`), so the caller runs nothing after it.
pub fn drainPending(session: *Session, line: []const u8) bool {
    var taken: [Session.max_pending]Attachment = undefined;
    const n = session.takePending(&taken);
    if (n == 0) return false;
    for (taken[0..n]) |at| adoptPending(session, at);
    ui.redraw(session);
    const cmd = commands.parseCommand(line);
    const verb = commands.verbOf(cmd.word);
    const bare_upload = cmd.arg.len == 0 and verb != null and verb.? == .upload;
    if (cmd.word.len != 0 and !bare_upload) return false; // a real command still has to run
    if (n > 1) logo.note("{d} images attached to this turn — '/images' lists them, '/prompt <text>' sends them all\n", .{n});
    return true;
}

/// Load one held image the way `/upload` does, consuming it: the encoded bytes become the
/// turn attachment (§2.1) and a copy backs the working image.
fn adoptPending(session: *Session, at: Attachment) void {
    const gpa = session.gpa;
    var owned = at;
    const img = image.decode(gpa, owned.bytes) catch |e| {
        logo.err("the pasted image \"{s}\" could not be decoded ({s})\n", .{ owned.label, @errorName(e) });
        owned.deinit(gpa);
        return;
    };
    const source: ?[]u8 = gpa.dupe(u8, owned.bytes) catch null;
    session.loadImage(img, owned.label, owned.temp, owned.fmt, source) catch |e| {
        logo.err("could not load the pasted image \"{s}\" ({s})\n", .{ owned.label, @errorName(e) }); // loadImage freed both
        owned.deinit(gpa);
        return;
    };
    session.addAttachment(owned.label, owned.bytes, owned.fmt, owned.temp) catch {}; // owns the bytes now
    gpa.free(owned.label);
}

/// Decode-check a pasted picture and hold it against the line (taking ownership of `bytes`),
/// reporting what landed. Only the ENCODED bytes are kept — the pixels are re-decoded if the
/// line is actually submitted, so an abandoned paste costs no more than the file it came from.
fn holdPending(session: *Session, bytes: []u8, label: []const u8, fmt: image.Format, temp: bool, out: []u8) ?usize {
    const gpa = session.gpa;
    var img = image.decode(gpa, bytes) catch |e| {
        logo.err("the pasted image could not be decoded ({s})\n", .{@errorName(e)});
        gpa.free(bytes);
        return null;
    };
    const w = img.width;
    const h = img.height;
    const kb = (bytes.len + 1023) / 1024;
    img.deinit(gpa);
    session.addPending(label, bytes, fmt, temp) catch return null; // addPending freed `bytes`
    const short = shortLabel(out, label);
    logo.note("attached {s} ({d}x{d} px · {d} KB) as image {d} — Backspace over its marker takes it back\n", .{ short, w, h, kb, session.pending.items.len });
    return short.len;
}

/// The name the line's marker shows: the last path component, middle-elided to fit `out`
/// (the editor sizes it) with the extension kept, and stripped of anything that would break
/// the marker's own `[…]` shape.
fn shortLabel(out: []u8, label: []const u8) []const u8 {
    const base = std.fs.path.basename(label);
    var n: usize = 0;
    if (base.len <= out.len) {
        @memcpy(out[0..base.len], base);
        n = base.len;
    } else {
        const ell = "…";
        const tail = @min(@as(usize, 8), out.len - ell.len - 1);
        const head = out.len - ell.len - tail;
        @memcpy(out[0..head], base[0..head]);
        @memcpy(out[head..][0..ell.len], ell);
        @memcpy(out[head + ell.len ..][0..tail], base[base.len - tail ..]);
        n = out.len;
    }
    for (out[0..n]) |*c| {
        if (c.* == '[' or c.* == ']' or (c.* < 0x20 and c.* > 0)) c.* = '_';
    }
    return out[0..n];
}

/// Whether a pasted path may become an image attachment: only while no command word has
/// settled yet, or under the two verbs that take pictures. Pasted into `/save …` a path is
/// still a path.
fn pathPasteAllowed(before: []const u8) bool {
    const cmd = commands.parseCommand(before);
    if (cmd.word.len == 0) return true;
    const verb = commands.verbOf(cmd.word) orelse return true; // half-typed, or not a command at all
    return verb == .prompt or verb == .upload;
}

// The cap a pasted PATH obeys — the same as a Ctrl-V's, but silent: a paste it cannot take is
// simply left as the text it was, which is never wrong.
fn pathPasteLimit(before: []const u8) usize {
    const cmd = commands.parseCommand(before);
    const verb = commands.verbOf(cmd.word);
    return if (verb != null and verb.? == .upload) 1 else line_edit.max_pending_images;
}

/// The single file path a paste carries, or null when it is ordinary text: outer quotes are
/// dropped and shell-style `\ ` escapes resolved (how a file manager's path arrives), while
/// an unescaped space — or a control byte — means prose.
fn unquotePath(out: []u8, text: []const u8) ?[]const u8 {
    var s = std.mem.trim(u8, text, " \t\r\n");
    if (s.len < 2 or s.len > out.len) return null;
    const quoted = (s[0] == '\'' or s[0] == '"') and s[s.len - 1] == s[0];
    if (quoted) s = s[1 .. s.len - 1];
    var n: usize = 0;
    var i: usize = 0;
    while (i < s.len) : (i += 1) {
        var c = s[i];
        if (c < 0x20 or c == 0x7f) return null;
        if (!quoted and c == '\\' and i + 1 < s.len) {
            i += 1;
            c = s[i];
        } else if (!quoted and c == ' ') {
            return null;
        }
        out[n] = c;
        n += 1;
    }
    return if (n == 0) null else out[0..n];
}

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


//! Images pasted INTO the line being typed (Ctrl-V, or a pasted path) — the editor owns the
//! `[Image #N <label>]` markers, the session owns the bytes behind them, and on submit they
//! become the turn's uploads.
const std = @import("std");
const image = @import("../../image.zig");
const pipeline = @import("../../pipeline.zig");
const logo = @import("../../logo.zig");
const core = @import("../../core.zig");
const clipboard = @import("../../clipboard.zig");
const commands = @import("../commands.zig");
const line_edit = @import("../../line_edit.zig");
const ui = @import("../ui.zig");
const Session = @import("../session.zig").Session;
const Attachment = @import("../session.zig").Attachment;
const handlers = @import("../handlers.zig");
const llmPrompt = @import("../llmPrompt.zig");
const clip = @import("clip.zig");

const clipboardToImage = clip.clipboardToImage;
const clipError = clip.clipError;

// The editor owns the `[Image #N <label>]` markers in the line, the session the bytes behind them.
// Nothing is loaded until the line is submitted (drainPending).

pub const max_paste_path = 1024; // longest pasted path we consider (well past PATH_MAX in practice)
pub const max_paste_bytes = 64 << 20; // 64 MiB cap on a pasted image file, matching the clipboard's

/// Ctrl-V while typing: hold the clipboard's picture against the line being edited. Writes the
/// marker's short label into `out` and returns its length; null when there was nothing to take.
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

/// How many images the line being typed may carry, with a printed reason when full: `/upload` takes
/// one, a `/prompt` (or no command word yet) three. The editor knows nothing about commands.
pub fn roomForPending(session: *Session, before: []const u8) bool {
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

/// A ⌘V paste that is nothing but an image FILE's path (how a terminal pastes a dragged picture).
/// Silent about what it does not accept: the paste is then ordinary text, which is what it meant.
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

/// Turn the images pasted into a SUBMITTED line into real uploads (§2.1), in marker order. True
/// when the paste WAS the whole command, so the caller runs nothing after it.
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
pub fn adoptPending(session: *Session, at: Attachment) void {
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

/// Decode-check a pasted picture and hold it against the line, taking ownership of `bytes`. Only
/// the ENCODED bytes are kept, so an abandoned paste costs no more than the file it came from.
pub fn holdPending(session: *Session, bytes: []u8, label: []const u8, fmt: image.Format, temp: bool, out: []u8) ?usize {
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

/// The name the line's marker shows: the last path component, middle-elided to fit `out` with the
/// extension kept, stripped of anything that would break the marker's own `[…]` shape.
pub fn shortLabel(out: []u8, label: []const u8) []const u8 {
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

/// Whether a pasted path may become an image attachment: only while no command word has settled,
/// or under the two verbs that take pictures. Pasted into `/save …` a path is still a path.
pub fn pathPasteAllowed(before: []const u8) bool {
    const cmd = commands.parseCommand(before);
    if (cmd.word.len == 0) return true;
    const verb = commands.verbOf(cmd.word) orelse return true; // half-typed, or not a command at all
    return verb == .prompt or verb == .upload;
}

// The cap a pasted PATH obeys — the same as a Ctrl-V's, but silent: a paste it cannot take is
// simply left as the text it was, which is never wrong.
pub fn pathPasteLimit(before: []const u8) usize {
    const cmd = commands.parseCommand(before);
    const verb = commands.verbOf(cmd.word);
    return if (verb != null and verb.? == .upload) 1 else line_edit.max_pending_images;
}

/// The single file path a paste carries, else null: outer quotes dropped and shell-style escaped
/// spaces resolved; an unescaped space — or a control byte — means prose.
pub fn unquotePath(out: []u8, text: []const u8) ?[]const u8 {
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

//! `Editor.readSecret`: one line read with nothing echoed — an API key typed or pasted at a
//! hidden prompt — and `Editor.maskCommitted`, which repaints a submitted line in its masked
//! form on the plain terminal. Bound as methods by line_edit.zig.
const std = @import("std");
const logo = @import("../app/logo.zig");
const restyle = @import("../console/render/ansi/restyle.zig");
const frame = @import("../console/screen/frame.zig");
const render = @import("render.zig");

const Editor = @import("line_edit.zig").Editor;

// How long an ESC waits for the byte that would make it a sequence (terminals send them together).
const lone_esc_ms = 30;

/// Ask `question` and read one line into `out`, no byte of it echoed. Enter ends it; Ctrl-C, Ctrl-D,
/// a lone Esc or a closed tty cancel (null); a bracketed paste lands whole.
pub fn readSecret(self: *Editor, question: []const u8, out: []u8) ?usize {
    if (self.screen) |s| frame.begin(s);
    if (self.screen != null) { // on the fixed prompt row, as a confirm asks
        self.gotoLineStart();
        self.writeAll("\x1b[2K");
    }
    var qbuf: [256]u8 = undefined;
    const q = std.fmt.bufPrint(&qbuf, "{s}{s}{s}", .{ logo.accentReal(), question, logo.resetSeq() }) catch question;
    self.writeAll(restyle.restyle(q));
    if (self.screen) |s| frame.end(s);
    var n: usize = 0;
    while (true) {
        const b = self.readByte() orelse return endSecret(self, question, null);
        switch (b) {
            '\r', '\n' => return endSecret(self, question, n),
            3, 4 => return endSecret(self, question, null), // Ctrl-C, Ctrl-D
            8, 127 => n -|= 1,
            21 => n = 0, // Ctrl-U
            27 => if (!escape(self, out, &n)) return endSecret(self, question, null),
            else => if (b >= 0x20 and n < out.len) {
                out[n] = b;
                n += 1;
            },
        }
    }
}

/// After an ESC: a bracketed paste is taken in whole, any other sequence (a mouse report, an
/// arrow) is dropped. False for a lone Esc, which cancels.
fn escape(self: *Editor, out: []u8, n: *usize) bool {
    const b1 = switch (self.pollByte(lone_esc_ms)) {
        .byte => |b| b,
        else => return false,
    };
    if (b1 != '[') return true;
    var params: [8]u8 = undefined;
    var pn: usize = 0;
    while (self.readByte()) |b| {
        if (b >= 0x40 and b <= 0x7e) {
            if (b == '~' and std.mem.eql(u8, params[0..pn], "200")) paste(self, out, n);
            return true;
        }
        if (pn < params.len) {
            params[pn] = b;
            pn += 1;
        }
    }
    return true;
}

/// The body of a bracketed paste, up to its `ESC[201~`: printable bytes kept, the rest dropped.
fn paste(self: *Editor, out: []u8, n: *usize) void {
    while (self.readByte()) |b| {
        if (b == 27) {
            var tail: [5]u8 = undefined; // "[201~"
            for (&tail) |*t| t.* = self.readByte() orelse return;
            if (std.mem.eql(u8, &tail, "[201~")) return;
            continue;
        }
        if (b < 0x20 or b == 0x7f or n.* >= out.len) continue;
        out[n.*] = b;
        n.* += 1;
    }
}

/// End the hidden prompt: the scrollback records the question and whether a key was given —
/// never the key.
fn endSecret(self: *Editor, question: []const u8, result: ?usize) ?usize {
    const said = if (result != null) "(entered, hidden)" else "(cancelled)";
    if (self.screen) |s| {
        frame.begin(s);
        defer frame.end(s);
        logo.print("{s}{s}\n", .{ question, said });
        self.gotoLineStart();
        self.writeAll("\x1b[2K");
    } else {
        self.writeAll(said);
        self.writeAll("\r\n");
    }
    return result;
}

/// Repaint the line just submitted as `shown`: on the plain terminal the typed text stays on
/// screen, so it is overwritten in place. `typed_len` bytes were typed after `prompt`.
pub fn maskCommitted(self: *Editor, prompt: []const u8, typed_len: usize, shown: []const u8) void {
    if (self.screen != null) return; // the full screen clears its prompt rows on submit
    var ws: std.posix.winsize = undefined;
    const ok = std.c.ioctl(self.fd_out, @intCast(@as(u32, std.posix.T.IOCGWINSZ)), &ws) == 0 and ws.col != 0;
    const cols: usize = if (ok) ws.col else 80;
    const rows = @max(1, (prompt.len + typed_len + cols - 1) / cols);
    var b: [32]u8 = undefined;
    self.writeAll(std.fmt.bufPrint(&b, "\x1b[{d}A\r\x1b[J", .{rows}) catch return);
    const cmd_end = std.mem.indexOfAny(u8, shown, " \t") orelse shown.len;
    render.writePromptRow(self, prompt, shown, cmd_end);
    self.writeAll("\r\n");
}

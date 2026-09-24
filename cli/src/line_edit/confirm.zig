//! `Editor.confirm`: the inline (Y/n) question and how its answer is recorded — on the
//! fixed prompt row in screen mode, in place otherwise. Bound as a method by line_edit.zig.
const std = @import("std");
const logo = @import("../app/logo.zig");
const restyle = @import("../console/render/ansi/restyle.zig");

const Editor = @import("../line_edit.zig").Editor;

/// Ask a yes/no question on the raw-mode tty and read a single keypress. 'y' or Enter
/// confirm (yes is the default); 'n', Esc or Ctrl-C decline. Used to guard `/upload`.
pub fn confirm(self: *Editor, question: []const u8) bool {
    if (self.screen != null) { // draw the question on the fixed prompt row
        self.gotoLineStart();
        self.writeAll("\x1b[2K");
    }
    var qbuf: [1024]u8 = undefined;
    const q = std.fmt.bufPrint(&qbuf, "{s}{s} (Y/n) {s}", .{ logo.accentReal(), question, logo.resetSeq() }) catch question;
    self.writeAll(restyle.restyle(q));
    while (true) {
        const ch = self.readByte() orelse return false; // closed tty -> treat as decline
        switch (ch) {
            'y', 'Y', '\r', '\n' => {
                self.finishConfirm(question, true);
                return true;
            },
            'n', 'N', 3 => { // 'n' or Ctrl-C
                self.finishConfirm(question, false);
                return false;
            },
            27 => { // Esc declines — but in screen mode a mouse report also starts with ESC,
                // so swallow a trailing CSI (ESC '[' … final) and ignore it rather than decline.
                if (self.screen != null) {
                    switch (self.pollByte(2)) {
                        .byte => |b2| if (b2 == '[') {
                            self.drainCsi();
                            continue;
                        },
                        else => {}, // lone Esc → fall through to decline
                    }
                }
                self.finishConfirm(question, false);
                return false;
            },
            else => {},
        }
    }
}

pub fn finishConfirm(self: *Editor, question: []const u8, yes: bool) void {
    if (self.screen != null) {
        logo.print("{s} {s}\n", .{ question, if (yes) "yes" else "no" }); // record in scrollback
        self.gotoLineStart();
        self.writeAll("\x1b[2K");
    } else {
        self.writeAll(if (yes) "yes\r\n" else "no\r\n");
    }
}

//! Untrusted text — a model's reply, a server's project name or keyword, a peer's edit — made
//! inert before it is printed: every control byte and escape sequence out, so it can neither
//! drive the terminal nor forge a console line. The rules are safety/sanitize.zig's.
const std = @import("std");
const sanitize = @import("../../safety/sanitize.zig");

/// Room for a name, a label, a keyword or an echoed path; longer text is cut, never mid-character.
pub const Buf = [512]u8;

/// `text` made inert in `buf`.
pub fn name(buf: *Buf, text: []const u8) []const u8 {
    const n = @min(text.len, buf.len);
    @memcpy(buf[0..n], text[0..n]);
    return sanitize.stripControls(buf, buf[0..n], .none);
}

/// A reply-sized `text` made inert in a copy the caller frees with `free`; the text itself when
/// no copy could be made and it was already clean, else the empty string.
pub fn copy(gpa: std.mem.Allocator, text: []const u8) Owned {
    const dup = gpa.dupe(u8, text) catch return .{ .text = if (isClean(text)) text else "" };
    return .{ .text = sanitize.stripControls(dup, dup, .none), .owned = dup };
}

pub const Owned = struct {
    text: []const u8,
    owned: ?[]u8 = null,
    pub fn free(self: Owned, gpa: std.mem.Allocator) void {
        if (self.owned) |o| gpa.free(o);
    }
};

fn isClean(text: []const u8) bool {
    for (text) |b| if ((b < 0x20 and b != '\n' and b != '\t') or b == 0x7f or b >= 0x80) return false;
    return true;
}

const testing = std.testing;

test "inert: a reply or a name reaches the terminal as text only" {
    var b: Buf = undefined;
    try testing.expectEqualStrings("poster", name(&b, "\x1b]52;c;ZXZpbA==\x07poster\x1b[2J"));
    const r = copy(testing.allocator, "done.\n\x1b]0;owned\x07Next?\x9b");
    defer r.free(testing.allocator);
    try testing.expectEqualStrings("done.\nNext?", r.text);
}

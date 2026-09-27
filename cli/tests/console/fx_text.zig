//! How an effect pin spells a frame: the synchronized-output wrappers taken off, control
//! bytes as `\xNN`, one painted row per line.
const std = @import("std");

// The synchronized-output wrappers a frame may carry; stripped, so only what is drawn is pinned.
const wrappers = [_][]const u8{ "\x1b[?2026h\x1b[?25l", "\x1b[?25h\x1b[?2026l" };

threadlocal var strip_buf: [1 << 20]u8 = undefined;

/// The frame without the synchronized-output wrappers; write boundaries are already gone.
pub fn stripWrappers(bytes: []const u8) []const u8 {
    var n: usize = 0;
    var i: usize = 0;
    outer: while (i < bytes.len) {
        for (wrappers) |w| if (std.mem.startsWith(u8, bytes[i..], w)) {
            i += w.len;
            continue :outer;
        };
        if (n == strip_buf.len) break;
        strip_buf[n] = bytes[i];
        n += 1;
        i += 1;
    }
    return strip_buf[0..n];
}

/// Control bytes as `\xNN`; with `rows`, a line break before each cursor placement so a frame
/// reads a row at a time.
pub fn escape(out: *std.ArrayList(u8), gpa: std.mem.Allocator, bytes: []const u8, rows: bool) void {
    for (bytes, 0..) |b, i| {
        if (rows and b == 0x1b and i != 0 and placesCursor(bytes[i..])) out.append(gpa, '\n') catch {};
        if (b == '\\') {
            out.appendSlice(gpa, "\\\\") catch {};
        } else if (b < 0x20 or b == 0x7f) {
            out.print(gpa, "\\x{x:0>2}", .{b}) catch {};
        } else out.append(gpa, b) catch {};
    }
}

fn placesCursor(s: []const u8) bool {
    if (s.len < 3 or s[1] != '[') return false;
    var j: usize = 2;
    while (j < s.len and ((s[j] >= '0' and s[j] <= '9') or s[j] == ';')) : (j += 1) {}
    return j < s.len and j > 2 and s[j] == 'H';
}

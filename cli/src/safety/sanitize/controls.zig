//! Untrusted text made inert for a terminal: every control byte and escape sequence that could
//! move the cursor, retitle the window, write the clipboard (OSC 52) or leave the alternate
//! screen is taken out. `sanitize.zig` re-exports it; the console's own colours may survive.
const std = @import("std");

/// How much of the escape vocabulary survives: nothing, or colour (CSI … m) and the console's
/// accent sentinel (0x01) — the full-screen scrollback's own output keeps those.
pub const Keep = enum { none, sgr };

/// `src` without C0 controls (TAB and LF kept), DEL, C1 (raw or UTF-8), malformed UTF-8 and
/// escape sequences — OSC/DCS/APC/PM/SOS through their terminator, CSI whole — written to `dst`,
/// which may alias `src`: the result is never longer.
pub fn stripControls(dst: []u8, src: []const u8, keep: Keep) []u8 {
    var o: usize = 0;
    var i: usize = 0;
    while (i < src.len) {
        const b = src[i];
        if (b == 0x1b) {
            const n = escapeLen(src, i);
            if (keep == .sgr and isSgr(src[i .. i + n])) {
                std.mem.copyForwards(u8, dst[o..][0..n], src[i..][0..n]);
                o += n;
            }
            i += n;
            continue;
        }
        if (b < 0x80) {
            const kept = b == '\n' or b == '\t' or (b == 0x01 and keep == .sgr) or (b >= 0x20 and b != 0x7f);
            if (kept) {
                dst[o] = b;
                o += 1;
            }
            i += 1;
            continue;
        }
        const n = std.unicode.utf8ByteSequenceLength(b) catch 1;
        const cp = if (i + n <= src.len) std.unicode.utf8Decode(src[i .. i + n]) catch null else null;
        if (cp) |c| {
            if (c < 0x80 or c > 0x9f) {
                std.mem.copyForwards(u8, dst[o..][0..n], src[i..][0..n]);
                o += n;
                i += n;
                continue;
            }
        }
        i += if (cp != null) n else 1; // a C1 control, or a byte that begins no character
    }
    return dst[0..o];
}

/// The length of the escape sequence at `s[i]` (an ESC): a CSI to its final byte, a string
/// sequence (OSC, DCS, APC, PM, SOS) to BEL or ST, else ESC and the one byte it introduces.
fn escapeLen(s: []const u8, i: usize) usize {
    if (i + 1 >= s.len) return 1;
    const kind = s[i + 1];
    var j = i + 2;
    switch (kind) {
        '[' => {
            while (j < s.len and !(s[j] >= 0x40 and s[j] <= 0x7e)) : (j += 1) {}
            return @min(j + 1, s.len) - i;
        },
        ']', 'P', '_', '^', 'X' => {
            while (j < s.len) : (j += 1) {
                if (s[j] == 0x07) return j + 1 - i;
                if (s[j] == 0x1b and j + 1 < s.len and s[j + 1] == '\\') return j + 2 - i;
            }
            return s.len - i;
        },
        else => return if (kind >= 0x20 and kind <= 0x7e) 2 else 1,
    }
}

/// A colour and nothing else: `ESC [` digits, `;` or `:` … `m`.
pub fn isSgr(seq: []const u8) bool {
    if (seq.len < 3 or seq[1] != '[' or seq[seq.len - 1] != 'm') return false;
    for (seq[2 .. seq.len - 1]) |c| if (!std.ascii.isDigit(c) and c != ';' and c != ':') return false;
    return true;
}

const testing = std.testing;

fn strip(src: []const u8, keep: Keep) []const u8 {
    const S = struct {
        var buf: [256]u8 = undefined;
    };
    @memcpy(S.buf[0..src.len], src);
    return stripControls(&S.buf, S.buf[0..src.len], keep);
}

test "stripControls: OSC, CSI and the rest of the escape vocabulary never reach the terminal" {
    try testing.expectEqualStrings("copied", strip("\x1b]52;c;ZXZpbA==\x07copied", .none));
    try testing.expectEqualStrings("title", strip("\x1b]0;pwned\x1b\\title", .none));
    try testing.expectEqualStrings("gone", strip("\x1b[?1049lgone", .sgr)); // leaves no alt screen
    try testing.expectEqualStrings("ab", strip("a\x1b[2J\x1b[Hb", .sgr));
    try testing.expectEqualStrings("xy", strip("x\x1bPdcs\x1b\\y", .none));
    try testing.expectEqualStrings("reset", strip("\x1bcreset", .none));
    try testing.expectEqualStrings("red", strip("\x1b[31mred\x1b[0m", .none));
    try testing.expectEqualStrings("\x1b[1;38;2;1;2;3mred\x1b[0m", strip("\x1b[1;38;2;1;2;3mred\x1b[0m", .sgr));
    try testing.expectEqualStrings("\x01note", strip("\x01note", .sgr));
    try testing.expectEqualStrings("note", strip("\x01note", .none));
}

test "stripControls: C0 but tab and newline, DEL, C1 and broken UTF-8 go; text stays whole" {
    try testing.expectEqualStrings("a\tb\nc", strip("a\tb\nc\x07\x08\x7f\r", .none));
    try testing.expectEqualStrings("csi", strip("\u{9b}31mcsi"[0..2] ++ "csi", .none)); // U+009B, UTF-8 encoded
    try testing.expectEqualStrings("ok", strip("\x9bok", .none)); // a raw C1 byte
    try testing.expectEqualStrings("é…🍋 ｀", strip("é…🍋 ｀", .none));
    try testing.expectEqualStrings("x", strip("\xc3x", .none)); // a lead byte with nothing after it
    try testing.expectEqualStrings("", strip("\x1b", .sgr));
}

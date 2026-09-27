//! The terminal's own selection highlight: asked for at start (OSC 17, with DA1 as the
//! sentinel that no late answer is left behind), tinted with the live accent while the
//! terminal selects, and handed back exactly as found.
const std = @import("std");
const sc = @import("../screen.zig");
const Screen = sc.Screen;
const logo = @import("../../app/logo.zig");
const skin = @import("../../app/skin.zig");
const ttyWrite = sc.ttyWrite;
const readByteTimeout = sc.Screen.readByteTimeout;

/// Ask the TERMINAL to paint its own selection in the live accent (OSC 17 sets the highlight
/// background, OSC 117 puts it back). Terminals without OSC 17 ignore it, so it is safe anywhere.
pub fn setSelectionTint(self: *Screen, on: bool) void {
    if (!logo.colorEnabled()) return;
    if (!on) {
        // Put back the exact colour the terminal had, when it told us; else ask for its
        // default with the reset opcode.
        if (self.saved_hl_len != 0) {
            var b: [80]u8 = undefined;
            const seq = std.fmt.bufPrint(&b, "\x1b]17;{s}\x1b\\", .{self.saved_hl[0..self.saved_hl_len]}) catch return;
            return ttyWrite(self.fd, seq);
        }
        return ttyWrite(self.fd, "\x1b]117\x1b\\");
    }
    const rgb = skin.washRgb(skin.get(), 0, logo.accentRgb());
    var buf: [40]u8 = undefined;
    const seq = std.fmt.bufPrint(&buf, "\x1b]17;#{x:0>2}{x:0>2}{x:0>2}\x1b\\", .{ rgb[0], rgb[1], rgb[2] }) catch return;
    ttyWrite(self.fd, seq);
}

/// Ask the terminal for its current highlight colour (`OSC 17;?` → `rgb:rrrr/gggg/bbbb`), then for
/// DA1, which every terminal answers, in order: once its reply is in, no late colour answer is left
/// to leak into the prompt. Best-effort with short deadlines; runs once, before anything is typed.
pub fn queryHighlight(self: *Screen) void {
    self.saved_hl_len = 0;
    if (!logo.colorEnabled()) return;
    const in = self.in_fd orelse return;
    ttyWrite(self.fd, "\x1b]17;?\x1b\\\x1b[c");
    var budget: usize = 512; // bytes read at most: a terminal flooding input cannot hold the start
    while (budget > 0) : (budget -= 1) {
        if ((readByteTimeout(in, 200) orelse return) != 0x1b) continue; // a key typed early
        switch (readByteTimeout(in, 120) orelse return) {
            ']' => readColour(self, in),
            '[' => while (budget > 0) : (budget -= 1) { // DA1: ESC [ ? … c — the end
                const b = readByteTimeout(in, 120) orelse return;
                if (b >= 0x40 and b <= 0x7e) {
                    if (b == 'c') return;
                    break;
                }
            },
            else => {},
        }
    }
}

// The OSC answer after its `ESC ]`: `17;<payload>` up to BEL or ST, kept when it is ours.
fn readColour(self: *Screen, in: std.posix.fd_t) void {
    const prefix = "17;";
    var matched: usize = 0;
    var n: usize = 0;
    while (true) {
        const b = readByteTimeout(in, 120) orelse return;
        if (b == 7) break; // BEL terminator
        if (b == 0x1b) { // ST: ESC \
            _ = readByteTimeout(in, 120);
            break;
        }
        if (matched < prefix.len) {
            matched = if (b == prefix[matched]) matched + 1 else prefix.len + 1;
            continue;
        }
        if (matched == prefix.len and n < self.saved_hl.len) {
            self.saved_hl[n] = b;
            n += 1;
        }
    }
    if (matched == prefix.len) self.saved_hl_len = n;
}

test "queryHighlight: the colour is kept, the DA1 reply ends the read, nothing is left over" {
    var threaded = std.Io.Threaded.init(std.testing.allocator, .{});
    defer threaded.deinit();
    const in = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(in[0]);
    defer _ = std.c.close(in[1]);
    const out = try std.Io.Threaded.pipe2(.{});
    defer _ = std.c.close(out[0]);
    defer _ = std.c.close(out[1]);
    var s = Screen{ .gpa = std.testing.allocator, .io = threaded.io(), .fd = out[1], .in_fd = in[0], .rows = 10, .cols = 40 };
    defer s.freeAll();
    logo.init(false, false);
    const replies = "\x1b]17;rgb:1111/2222/3333\x1b\\\x1b[?62;22cX";
    _ = std.c.write(in[1], replies, replies.len);
    s.queryHighlight();
    try std.testing.expectEqualStrings("rgb:1111/2222/3333", s.saved_hl[0..s.saved_hl_len]);
    var left: [4]u8 = undefined;
    try std.testing.expectEqual(@as(isize, 1), std.c.read(in[0], &left, left.len)); // only the key after
    try std.testing.expectEqual(@as(u8, 'X'), left[0]);
    // A terminal without OSC 17 answers DA1 alone: nothing kept, still done at once.
    const da1 = "\x1b[?1;2c";
    _ = std.c.write(in[1], da1, da1.len);
    s.queryHighlight();
    try std.testing.expectEqual(@as(usize, 0), s.saved_hl_len);
}

const testing = std.testing;

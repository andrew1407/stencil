//! A painted row re-dressed in the active secret skin (app/skin.zig): the matrix and blue
//! screen rewrite every colour escape to their own fg + bg, the rainbow paints each accent
//! cell in its column's hue (the bifrost and fairy lights by frame too), fruit, meow and pie
//! swap each accent letter for a picture. Gold (a typed secret) survives every skin.
const std = @import("std");
const logo = @import("../../../app/logo.zig");
const skin = @import("../../../app/skin.zig");
const scan = @import("scan.zig");

const appendBytes = scan.appendBytes;
const utf8Len = scan.utf8Len;
const csiLen = scan.csiLen;

threadlocal var scratch: [32768]u8 = undefined;

/// `line` in the active skin; `line` itself when no skin is on, colour is off, or it will not fit.
pub fn restyle(line: []const u8) []const u8 {
    return restyleInto(line, skin.get(), logo.accentReal(), true, &scratch);
}

/// The input row: pictures never stand in for what the user types.
pub fn restyleInput(line: []const u8) []const u8 {
    return restyleInto(line, skin.get(), logo.accentReal(), false, &scratch);
}

/// The reset a clipped row ends on: the skin's cell style, so the erase after it fills its bg.
pub fn tail() []const u8 {
    if (!logo.colorEnabled()) return "";
    const t = skin.traitsOf(skin.get());
    return if (t.paints_cells) t.base else "\x1b[0m";
}

/// Under the picture skins the accent itself turns gold — borders, rules, the prompt — and, when
/// `letters`, each accent letter becomes a picture.
pub fn restyleInto(line: []const u8, s: skin.Skin, accent: []const u8, letters: bool, out: []u8) []const u8 {
    if (!skin.restyles(s) or !logo.colorEnabled()) return line;
    const t = skin.traitsOf(s);
    var oi: usize = 0;
    var col: usize = 0;
    var run = false; // inside an accent span the rainbow and the pictures work cell by cell
    const pictures = t.replaces_letters;
    const per_cell = t.paints_runs or pictures;
    const swap = pictures and letters;
    const row_key = std.hash.Wyhash.hash(0, line);
    var prev: ?usize = null;
    if (t.paints_cells) appendBytes(out, &oi, t.base);
    var i: usize = 0;
    while (i < line.len) {
        const b = line[i];
        if (b == 0x01) { // the accent sentinel
            if (pictures) appendBytes(out, &oi, skin.gold_accent);
            if (per_cell) run = true else appendBytes(out, &oi, t.base);
            i += 1;
            continue;
        }
        const esc = csiLen(line, i);
        if (esc != 0) {
            const seq = line[i .. i + esc];
            i += esc;
            if (seq[seq.len - 1] != 'm' or std.mem.eql(u8, seq, skin.gold)) {
                appendBytes(out, &oi, seq);
                run = false;
            } else if (per_cell) {
                run = accent.len != 0 and std.mem.eql(u8, seq, accent);
                if (!run) appendBytes(out, &oi, seq) else if (pictures) appendBytes(out, &oi, skin.gold_accent);
            } else {
                const p = params(seq);
                appendBytes(out, &oi, if (p.reverse) t.title else t.base);
                if (p.bold) appendBytes(out, &oi, "\x1b[1m");
            }
            continue;
        }
        const clen = @min(utf8Len(b), line.len - i);
        if (run and swap and b > ' ' and b < 0x7f) {
            const k = skin.pictureAt(s, row_key, col, prev);
            prev = k;
            appendBytes(out, &oi, skin.picture(s, k));
            i += 1;
            col += 2;
            // Terminals disagree on an emoji's width; placing the cursor keeps the row's columns.
            var cb: [16]u8 = undefined;
            appendBytes(out, &oi, std.fmt.bufPrint(&cb, "\x1b[{d}G", .{col + 1}) catch "");
            continue;
        }
        if (run and !pictures and b != ' ') {
            var hb: [24]u8 = undefined;
            appendBytes(out, &oi, skin.cellSgr(s, col, &hb));
        }
        if (oi + clen > out.len) return line;
        @memcpy(out[oi..][0..clen], line[i..][0..clen]);
        col += scan.cellWidth(line, i);
        oi += clen;
        i += clen;
    }
    return out[0..oi];
}

const Params = struct { bold: bool = false, reverse: bool = false };

// The SGR attributes a skin keeps; a 38/48 colour's own numbers are skipped, never read as one.
fn params(seq: []const u8) Params {
    var p = Params{};
    var it = std.mem.splitScalar(u8, seq[2 .. seq.len - 1], ';');
    while (it.next()) |tok| {
        const n = std.fmt.parseInt(u16, tok, 10) catch continue;
        switch (n) {
            1 => p.bold = true,
            7 => p.reverse = true,
            38, 48 => {
                const kind = it.next() orelse break;
                const skip: usize = if (std.mem.eql(u8, kind, "2")) 3 else 1;
                for (0..skip) |_| _ = it.next();
            },
            else => {},
        }
    }
    return p;
}

const testing = std.testing;

test "restyle: no skin leaves the row untouched" {
    var out: [256]u8 = undefined;
    const line = "\x01> \x1b[0mhello";
    try testing.expectEqual(line.ptr, restyleInto(line, .none, "\x1b[31m", true, &out).ptr);
}

test "restyle: the matrix rewrites every colour but gold, keeps bold, reads rgb 1 as no bold" {
    var out: [512]u8 = undefined;
    const base = comptime skin.traitsOf(.matrix).base;
    const got = restyleInto("\x1b[38;2;1;2;3mA\x1b[1;31mB" ++ skin.gold ++ "C\x1b[0mD", .matrix, "", true, &out);
    try testing.expectEqualStrings(base ++ base ++ "A" ++ base ++ "\x1b[1mB" ++ skin.gold ++ "C" ++ base ++ "D", got);
}

test "restyle: the blue screen turns reverse video into its grey title bar" {
    var out: [256]u8 = undefined;
    const got = restyleInto("\x1b[7m Stencil \x1b[0m", .bluescreen, "", true, &out);
    const blue = comptime skin.traitsOf(.bluescreen);
    const want = comptime blue.base ++ blue.title ++ " Stencil " ++ blue.base;
    try testing.expectEqualStrings(want, got);
}

test "restyle: the rainbow colours accent cells by column and leaves the rest alone" {
    var out: [512]u8 = undefined;
    const accent = "\x1b[38;2;9;9;9m";
    const got = restyleInto("ab" ++ accent ++ "c d\x1b[0me", .rainbow, accent, true, &out);
    var h2: [24]u8 = undefined;
    var h4: [24]u8 = undefined;
    const c2 = skin.cellSgr(.rainbow, 2, &h2);
    const c4 = skin.cellSgr(.rainbow, 4, &h4);
    var want: [256]u8 = undefined;
    const w = try std.fmt.bufPrint(&want, "ab{s}c {s}d\x1b[0me", .{ c2, c4 });
    try testing.expectEqualStrings(w, got);
}

test "restyle: fruit swaps accent letters only, never twice the same fruit in a row" {
    var out: [1024]u8 = undefined;
    var again: [1024]u8 = undefined;
    const accent = "\x1b[38;2;9;9;9m";
    const line = "ab" ++ accent ++ "cdef g\x1b[0mh";
    const got = restyleInto(line, .fruit, accent, true, &out);
    try testing.expectEqualStrings(got, restyleInto(line, .fruit, accent, true, &again));
    const body = got[2 + skin.gold_accent.len .. got.len - "\x1b[0mh".len];
    try testing.expect(std.mem.startsWith(u8, got, "ab" ++ skin.gold_accent));
    var prev: []const u8 = "";
    var n: usize = 0;
    var i: usize = 0;
    while (i < body.len) {
        if (body[i] == ' ') {
            i += 1;
            continue;
        }
        const esc = csiLen(body, i); // the cursor placed after each picture
        if (esc != 0) {
            i += esc;
            continue;
        }
        // Longest first, so the lime is not read as a lemon.
        var best: []const u8 = "";
        for (skin.fruits) |f| if (f.len > best.len and std.mem.startsWith(u8, body[i..], f)) {
            best = f;
        };
        try testing.expect(best.len != 0);
        try testing.expect(!std.mem.eql(u8, best, prev));
        prev = best;
        n += 1;
        i += best.len;
    }
    try testing.expectEqual(@as(usize, 5), n); // one fruit per letter
}

test "restyle: gold — secret words, their list and phrases — stays gold on every skin" {
    var out: [512]u8 = undefined;
    const line = skin.gold ++ "Meow)\x1b[0m";
    for ([_]skin.Skin{ .matrix, .bluescreen, .rainbow, .bifrost, .fruit, .meow }) |s| {
        try testing.expect(std.mem.indexOf(u8, restyleInto(line, s, "", true, &out), skin.gold ++ "Meow)") != null);
    }
}

test "restyle: under the picture skins the rest of the accent turns gold, and input keeps its letters" {
    var out: [512]u8 = undefined;
    const accent = "\x1b[38;2;9;9;9m";
    const rule = accent ++ "━━━\x1b[0m";
    for ([_]skin.Skin{ .fruit, .meow, .pie }) |s| {
        try testing.expectEqualStrings(skin.gold_accent ++ "━━━\x1b[0m", restyleInto(rule, s, accent, true, &out));
        try testing.expectEqualStrings(skin.gold_accent ++ "> /x\x1b[0m", restyleInto(accent ++ "> /x\x1b[0m", s, accent, false, &out));
    }
}

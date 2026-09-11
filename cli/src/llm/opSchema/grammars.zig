//! The registry's token grammars (registry.regexes), hand-matched — Zig has no regex.
//! Each function's doc comment is the JS regex it must accept and reject identically.
const std = @import("std");

pub const Grammar = enum { CROP_TOKEN, CROP_ASPECT, PAGE_FORMAT, HEX, CSS_NAME, FORMULA_X, FORMULA_Y, HTTP_URL, URL_SCHEME };

pub fn matches(g: Grammar, s: []const u8) bool {
    return switch (g) {
        .CROP_TOKEN => isCropToken(s),
        .CROP_ASPECT => isAspectToken(s),
        .PAGE_FORMAT => isPageFormat(s),
        .HEX => isHex6(s),
        .CSS_NAME => s.len != 0 and allAlpha(s),
        .FORMULA_X => isFormula(s, 'x'),
        .FORMULA_Y => isFormula(s, 'y'),
        .HTTP_URL => isHttpUrl(s),
        .URL_SCHEME => hasUrlScheme(s),
    };
}

pub fn grammar(name: []const u8) Grammar {
    return std.meta.stringToEnum(Grammar, name) orelse std.debug.panic("opRegistry: unknown grammar \"{s}\"", .{name});
}

/// `^-?(\d+(\.\d+)?|\.\d+)(%|px|cm|in)?$`
fn isCropToken(token: []const u8) bool {
    const rest = if (std.mem.startsWith(u8, token, "-")) token[1..] else token;
    var i: usize = 0;
    while (i < rest.len and (std.ascii.isDigit(rest[i]) or rest[i] == '.')) : (i += 1) {}
    const number = rest[0..i];
    const unit = rest[i..];
    const valid_number = blk: {
        if (std.mem.indexOfScalar(u8, number, '.')) |dot| {
            const int = number[0..dot];
            const frac = number[dot + 1 ..];
            break :blk frac.len != 0 and allDigits(frac) and allDigits(int);
        }
        break :blk number.len != 0 and allDigits(number);
    };
    const valid_unit = unit.len == 0 or std.mem.eql(u8, unit, "%") or
        std.mem.eql(u8, unit, "px") or std.mem.eql(u8, unit, "cm") or std.mem.eql(u8, unit, "in");
    return valid_number and valid_unit;
}

fn allDigits(s: []const u8) bool {
    for (s) |c| {
        if (!std.ascii.isDigit(c)) return false;
    }
    return true;
}

/// `^0*[1-9]\d*:0*[1-9]\d*$` — both sides > 0.
fn isAspectToken(token: []const u8) bool {
    const colon = std.mem.indexOfScalar(u8, token, ':') orelse return false;
    const w = token[0..colon];
    const h = token[colon + 1 ..];
    if (w.len == 0 or h.len == 0 or !allDigits(w) or !allDigits(h)) return false;
    return std.mem.indexOfNone(u8, w, "0") != null and std.mem.indexOfNone(u8, h, "0") != null;
}

/// `^[abc](10|[0-9])$`
fn isPageFormat(format: []const u8) bool {
    if (format.len < 2) return false;
    const series = format[0];
    if (series != 'a' and series != 'b' and series != 'c') return false;
    const number = format[1..];
    if (std.mem.eql(u8, number, "10")) return true;
    return number.len == 1 and std.ascii.isDigit(number[0]);
}

/// `^#[0-9a-fA-F]{6}$`
fn isHex6(color: []const u8) bool {
    if (color.len != 7 or color[0] != '#') return false;
    for (color[1..]) |c| {
        if (!std.ascii.isHex(c)) return false;
    }
    return true;
}

fn allAlpha(s: []const u8) bool {
    for (s) |c| {
        if (!std.ascii.isAlphabetic(c)) return false;
    }
    return true;
}

/// `^[0-9x+\-*/(). ]+$` (or y): the single variable matching the axis.
fn isFormula(s: []const u8, axis: u8) bool {
    if (s.len == 0) return false;
    for (s) |c| {
        if (!(std.ascii.isDigit(c) or c == axis or std.mem.indexOfScalar(u8, "+-*/(). ", c) != null)) return false;
    }
    return true;
}

/// `^[hH][tT][tT][pP][sS]?://\S+$`
fn isHttpUrl(url: []const u8) bool {
    const scheme_len: usize = if (std.ascii.startsWithIgnoreCase(url, "https://"))
        "https://".len
    else if (std.ascii.startsWithIgnoreCase(url, "http://"))
        "http://".len
    else
        return false;
    if (url.len == scheme_len) return false;
    return !hasWhitespace(url);
}

/// JS `\s`: ASCII whitespace plus the Unicode spaces and line separators.
fn hasWhitespace(s: []const u8) bool {
    var it = std.unicode.Utf8View.initUnchecked(s).iterator();
    while (it.nextCodepoint()) |cp| {
        const ws = switch (cp) {
            0x09...0x0D, 0x20, 0xA0, 0x1680, 0x2000...0x200A, 0x2028, 0x2029, 0x202F, 0x205F, 0x3000, 0xFEFF => true,
            else => false,
        };
        if (ws) return true;
    }
    return false;
}

/// `^[a-zA-Z][a-zA-Z0-9+.\-]*://` — a prefix match.
fn hasUrlScheme(s: []const u8) bool {
    if (s.len == 0 or !std.ascii.isAlphabetic(s[0])) return false;
    var i: usize = 1;
    while (i < s.len and (std.ascii.isAlphanumeric(s[i]) or s[i] == '+' or s[i] == '.' or s[i] == '-')) : (i += 1) {}
    return std.mem.startsWith(u8, s[i..], "://");
}

const testing = std.testing;

test "grammars: the registry's token regexes, hand-matched" {
    try testing.expect(matches(.CROP_TOKEN, "10"));
    try testing.expect(matches(.CROP_TOKEN, "-10%"));
    try testing.expect(matches(.CROP_TOKEN, ".5in"));
    try testing.expect(matches(.CROP_TOKEN, "1.5cm"));
    try testing.expect(!matches(.CROP_TOKEN, ""));
    try testing.expect(!matches(.CROP_TOKEN, "-"));
    try testing.expect(!matches(.CROP_TOKEN, "1.")); // a dot needs a fraction
    try testing.expect(!matches(.CROP_TOKEN, "1..5"));
    try testing.expect(!matches(.CROP_TOKEN, "10 %"));
    try testing.expect(!matches(.CROP_TOKEN, "10pt"));
    try testing.expect(!matches(.CROP_TOKEN, "+10"));

    try testing.expect(matches(.CROP_ASPECT, "4:3") and matches(.CROP_ASPECT, "04:3"));
    inline for (.{ "0:3", "4:0", "-1:2", "3:4:5", "a:b", "1.5:2", "4", "4:", ":3", "1e2:3", "" }) |s|
        try testing.expect(!matches(.CROP_ASPECT, s));

    try testing.expect(matches(.PAGE_FORMAT, "a0") and matches(.PAGE_FORMAT, "b10"));
    try testing.expect(!matches(.PAGE_FORMAT, "a") and !matches(.PAGE_FORMAT, "a11") and !matches(.PAGE_FORMAT, "d4") and !matches(.PAGE_FORMAT, "A4"));

    try testing.expect(matches(.HEX, "#A1b2c3") and !matches(.HEX, "#0ff") and !matches(.HEX, "00ff00"));
    try testing.expect(matches(.CSS_NAME, "pink") and !matches(.CSS_NAME, "") and !matches(.CSS_NAME, "hot pink"));
    try testing.expect(matches(.FORMULA_X, "x*2 + (1)") and !matches(.FORMULA_X, "y*2") and !matches(.FORMULA_X, "") and !matches(.FORMULA_X, "x^2"));
    try testing.expect(matches(.FORMULA_Y, "y**2"));

    try testing.expect(matches(.HTTP_URL, "HTTP://b.example/x.jpg") and matches(.HTTP_URL, "https://a/b"));
    try testing.expect(!matches(.HTTP_URL, "https://") and !matches(.HTTP_URL, "https://a.example/a b") and !matches(.HTTP_URL, "ftp://a/x"));
    try testing.expect(!matches(.HTTP_URL, "https://a.example/a\u{00a0}b")); // JS \s is Unicode-aware

    try testing.expect(matches(.URL_SCHEME, "https://x") and matches(.URL_SCHEME, "file:///etc/passwd") and matches(.URL_SCHEME, "x+y.z-1://"));
    try testing.expect(!matches(.URL_SCHEME, "~/Downloads") and !matches(.URL_SCHEME, "C:\\x") and !matches(.URL_SCHEME, "1http://x") and !matches(.URL_SCHEME, "a:/b"));
}

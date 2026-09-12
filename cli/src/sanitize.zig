//! The ONE sanitizer for untrusted server/provider prose that may be printed: the LLM
//! transport's error details and serverClient's rejection messages both go through it.
//! Pinned by the shared sanitizer fixture corpus (tests/sanitizer_fixtures_test.zig).
const std = @import("std");

/// How much of a provider's own prose an error may quote (server upstream.go parity).
pub const detail_limit = 200;
pub const DetailBuf = [detail_limit + "…".len]u8;

/// Untrusted provider prose made safe to print: control chars/newlines out (no forged
/// console lines), URL- and token-shaped words redacted (an endpoint may echo the key
/// back), whitespace collapsed, cut at `detail_limit` on a word boundary.
pub fn sanitizeDetail(text: []const u8, out: *DetailBuf) []const u8 {
    const src = text[0..@min(text.len, 4 * detail_limit)];
    var n: usize = 0;
    var i: usize = 0;
    while (nextWord(src, &i)) |w| {
        var word: []const u8 = if (secretish(w)) "[redacted]" else w;
        // Browser parity: "bearer <cred>"/"basic <cred>" collapses to ONE [redacted].
        if (isAuthScheme(w)) {
            var j = i;
            if (nextWord(src, &j)) |cred| {
                if (credRun(cred) >= 8) {
                    word = "[redacted]";
                    i = j;
                }
            }
        }
        const sep: usize = if (n == 0) 0 else 1;
        if (n + sep + word.len > detail_limit) {
            @memcpy(out[n..][0.."…".len], "…");
            return out[0 .. n + "…".len];
        }
        if (sep == 1) {
            out[n] = ' ';
            n += 1;
        }
        @memcpy(out[n..][0..word.len], word);
        n += word.len;
    }
    return out[0..n];
}

/// Word separators: spaces and every control byte (DEL included).
fn isDetailSep(c: u8) bool {
    return c <= ' ' or c == 0x7f;
}

/// The next separator-delimited word from `src`, advancing `i` past it; null at the end.
fn nextWord(src: []const u8, i: *usize) ?[]const u8 {
    while (i.* < src.len and isDetailSep(src[i.*])) i.* += 1;
    const start = i.*;
    while (i.* < src.len and !isDetailSep(src[i.*])) i.* += 1;
    return if (i.* == start) null else src[start..i.*];
}

/// The browser sanitizer's auth-scheme words: a following credential is redacted.
fn isAuthScheme(word: []const u8) bool {
    return std.ascii.eqlIgnoreCase(word, "bearer") or std.ascii.eqlIgnoreCase(word, "basic");
}

/// Leading run of the browser's bearer-credential chars (`A-Za-z0-9._~+/=-`).
fn credRun(word: []const u8) usize {
    for (word, 0..) |c, k| {
        if (!std.ascii.isAlphanumeric(c) and std.mem.indexOfScalar(u8, "._~+/=-", c) == null) return k;
    }
    return word.len;
}

/// Leading run of token chars (`A-Za-z0-9._-`).
fn tokenRun(word: []const u8) usize {
    for (word, 0..) |c, k| {
        if (!std.ascii.isAlphanumeric(c) and c != '.' and c != '_' and c != '-') return k;
    }
    return word.len;
}

/// True for a word that must not be echoed: an absolute URL (an internal endpoint is
/// not the user's business), a long opaque run, or a credential pair — one of the
/// browser's key heads (`sk|pk|api[-_]?key|key|token|secret`) + `-_=:` + a 6+ token run.
fn secretish(word: []const u8) bool {
    if (std.mem.indexOf(u8, word, "://") != null) return true;
    if (word.len >= 24 and isTokenChars(word)) return true;
    const names = [_][]const u8{ "api_key", "api-key", "apikey", "sk", "pk", "key", "token", "secret" };
    for (names) |name| {
        if (word.len < name.len + 1 + 6) continue;
        if (!std.ascii.startsWithIgnoreCase(word, name)) continue;
        if (std.mem.indexOfScalar(u8, "-_=:", word[name.len]) == null) continue;
        if (tokenRun(word[name.len + 1 ..]) >= 6) return true;
    }
    return false;
}

/// True when every byte is a token character (`A-Za-z0-9._-`).
fn isTokenChars(s: []const u8) bool {
    for (s) |c| {
        if (!std.ascii.isAlphanumeric(c) and c != '.' and c != '_' and c != '-') return false;
    }
    return true;
}

const testing = std.testing;

test "sanitizeDetail: bounded, control-free, and never echoing a key or URL" {
    var buf: DetailBuf = undefined;
    try testing.expectEqualStrings("model not found", sanitizeDetail("model\nnot\tfound", &buf));
    try testing.expectEqualStrings(
        "Incorrect API key provided: [redacted]",
        sanitizeDetail("Incorrect API key provided: sk-abcdef1234567890", &buf),
    );
    try testing.expectEqualStrings("see [redacted] now", sanitizeDetail("see http://10.0.0.5:11434/api/chat now", &buf));
    // Browser-parity rules: bearer/basic + credential collapse to one [redacted];
    // compound api_key/api-key heads gate the pair rule too.
    try testing.expectEqualStrings(
        "authorization [redacted] was rejected",
        sanitizeDetail("authorization Bearer abcdef1234567890 was rejected", &buf),
    );
    try testing.expectEqualStrings("Bearer abc kept", sanitizeDetail("Bearer abc kept", &buf));
    try testing.expectEqualStrings(
        "request had [redacted] attached",
        sanitizeDetail("request had api_key=supersecretvalue1 attached", &buf),
    );
    try testing.expectEqualStrings(
        "and [redacted] too",
        sanitizeDetail("and api-key:secret99 too", &buf),
    );
    try testing.expectEqualStrings(
        "Check your key and try again.",
        sanitizeDetail("Check your key and try again.", &buf),
    );
    var long: [900]u8 = undefined;
    for (&long, 0..) |*c, i| c.* = if (i % 5 == 4) ' ' else 'a';
    const cut = sanitizeDetail(&long, &buf);
    try testing.expect(cut.len <= detail_limit + "…".len);
    try testing.expect(std.mem.endsWith(u8, cut, "…"));
}

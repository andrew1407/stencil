//! Finding the plan inside a chat reply: strip Markdown fences, then take the first
//! BALANCED JSON object — prose on either side of it is ignored, never parsed.
const std = @import("std");

// Extraction helpers (fences + the first balanced object)

/// Remove Markdown code fences (``` with an optional language tag), keeping the rest of
/// the text intact — the JS reference's `raw.replace(/```[a-zA-Z]*/g, '')`.
pub fn stripFences(a: std.mem.Allocator, text: []const u8) error{OutOfMemory}![]u8 {
    var out: std.ArrayList(u8) = .empty;
    var rest = text;
    while (std.mem.indexOf(u8, rest, "```")) |i| {
        try out.appendSlice(a, rest[0..i]);
        rest = rest[i + 3 ..];
        var n: usize = 0;
        while (n < rest.len and std.ascii.isAlphabetic(rest[n])) : (n += 1) {}
        rest = rest[n..];
    }
    try out.appendSlice(a, rest);
    return out.toOwnedSlice(a);
}

/// The first balanced `{ … }` slice (string- and escape-aware), or null.
pub fn firstJsonObject(text: []const u8) ?[]const u8 {
    const start = std.mem.indexOfScalar(u8, text, '{') orelse return null;
    var depth: usize = 0;
    var in_string = false;
    var escaped = false;
    for (text[start..], start..) |c, i| {
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        switch (c) {
            '"' => in_string = true,
            '{' => depth += 1,
            '}' => {
                depth -= 1;
                if (depth == 0) return text[start .. i + 1];
            },
            else => {},
        }
    }
    return null;
}

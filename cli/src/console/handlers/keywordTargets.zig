//! Pure target-spec parsing for the `/keywords*` verbs: `<project | ["a","b"]> <keyword...>`
//! splitting, name collection, and the keyword listing lines.
const std = @import("std");
const logo = @import("../../logo.zig");
const msg = @import("../../messages.zig");

pub const KwMode = enum { add, del };

/// Case-insensitive substring test (std.mem has no case-insensitive `contains`).
pub fn containsIgnoreCase(haystack: []const u8, needle: []const u8) bool {
    if (needle.len == 0) return true;
    if (needle.len > haystack.len) return false;
    var i: usize = 0;
    while (i + needle.len <= haystack.len) : (i += 1) {
        if (std.ascii.eqlIgnoreCase(haystack[i .. i + needle.len], needle)) return true;
    }
    return false;
}

/// Print a project's keyword set (or "(none)").
pub fn printKeywordCsv(keywords: []const []const u8) void {
    for (keywords, 0..) |k, i| logo.print("{s} {s}", .{ if (i == 0) "" else ",", k });
}

pub fn printKeywords(name: []const u8, keywords: []const []const u8) void {
    if (keywords.len == 0) {
        logo.print(msg.keywords_none, .{name});
        return;
    }
    logo.print(msg.keywords_head, .{name});
    printKeywordCsv(keywords);
    logo.print("\n", .{});
}

/// Split `<project | ["a","b"]> <keyword...>` into the target spec and keyword remainder (both
/// slices into `arg`). A leading '[' captures up to the matching ']'; a leading '"' a quoted
/// name; else the first whitespace token.
pub fn splitTargetSpec(arg: []const u8) struct { target: []const u8, rest: []const u8 } {
    const a = std.mem.trim(u8, arg, " \t");
    if (a.len == 0) return .{ .target = "", .rest = "" };
    if (a[0] == '[') {
        if (std.mem.indexOfScalar(u8, a, ']')) |end|
            return .{ .target = a[0 .. end + 1], .rest = std.mem.trim(u8, a[end + 1 ..], " \t,") };
        return .{ .target = a, .rest = "" };
    }
    if (a[0] == '"') {
        if (std.mem.indexOfScalarPos(u8, a, 1, '"')) |end|
            return .{ .target = a[1..end], .rest = std.mem.trim(u8, a[end + 1 ..], " \t,") };
        return .{ .target = a[1..], .rest = "" };
    }
    if (std.mem.indexOfAny(u8, a, " \t")) |sp|
        return .{ .target = a[0..sp], .rest = std.mem.trim(u8, a[sp + 1 ..], " \t") };
    return .{ .target = a, .rest = "" };
}

/// Collect project names from a target spec: a bracketed comma list, or a single name. Names are
/// slices into `spec`; the returned ArrayList must be deinit'd by the caller.
pub fn collectTargetNames(gpa: std.mem.Allocator, spec: []const u8) !std.ArrayList([]const u8) {
    var out: std.ArrayList([]const u8) = .empty;
    errdefer out.deinit(gpa);
    const s = std.mem.trim(u8, spec, " \t");
    if (s.len != 0 and s[0] == '[') {
        const inner = if (s[s.len - 1] == ']') s[1 .. s.len - 1] else s[1..];
        var it = std.mem.tokenizeScalar(u8, inner, ',');
        while (it.next()) |tok| {
            const name = std.mem.trim(u8, tok, " \t\"");
            if (name.len != 0) try out.append(gpa, name);
        }
    } else {
        const name = std.mem.trim(u8, s, "\" \t");
        if (name.len != 0) try out.append(gpa, name);
    }
    return out;
}

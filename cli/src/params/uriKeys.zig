//! The launch URI's vocabulary: common/config/launchQuery.json as Zig tables (canonical key ←
//! spellings, the fragment-only secrets, the theme and chat words), pinned to the asset by
//! tests/config/launch_query_drift_test.zig, plus the script cap from constants.json.
const std = @import("std");
const constants_json = @embedFile("constants.json");

pub const Spelling = struct { canonical: []const u8, word: []const u8 };
pub const spellings = [_]Spelling{
    .{ .canonical = "server", .word = "server" },                 .{ .canonical = "id", .word = "id" },
    .{ .canonical = "version", .word = "version" },               .{ .canonical = "project", .word = "project" },
    .{ .canonical = "source", .word = "source" },                 .{ .canonical = "source", .word = "src" },
    .{ .canonical = "frame", .word = "frame" },                   .{ .canonical = "blank", .word = "blank" },
    .{ .canonical = "width", .word = "width" },                   .{ .canonical = "height", .word = "height" },
    .{ .canonical = "layout", .word = "layout" },                 .{ .canonical = "layout-raw", .word = "layout-raw" },
    .{ .canonical = "incognito", .word = "incognito" },           .{ .canonical = "script", .word = "script" },
    .{ .canonical = "script-mode", .word = "script-mode" },       .{ .canonical = "name", .word = "name" },
    .{ .canonical = "description", .word = "description" },       .{ .canonical = "description", .word = "desc" },
    .{ .canonical = "keywords", .word = "keywords" },             .{ .canonical = "project-color", .word = "project-color" },
    .{ .canonical = "project-color", .word = "color" },           .{ .canonical = "expire", .word = "expire" },
    .{ .canonical = "expire", .word = "expiration" },             .{ .canonical = "expire", .word = "expires" },
    .{ .canonical = "theme", .word = "theme" },                   .{ .canonical = "main-color", .word = "main-color" },
    .{ .canonical = "main-color", .word = "accent" },             .{ .canonical = "connect", .word = "connect" },
    .{ .canonical = "connect", .word = "connection" },            .{ .canonical = "token", .word = "token" },
    .{ .canonical = "llm-provider", .word = "llm-provider" },     .{ .canonical = "llm-model", .word = "llm-model" },
    .{ .canonical = "llm-base-url", .word = "llm-base-url" },     .{ .canonical = "llm-base-url", .word = "llm-url" },
    .{ .canonical = "llm-server-url", .word = "llm-server-url" }, .{ .canonical = "llm-server-url", .word = "llm-server" },
    .{ .canonical = "llm-api-key", .word = "llm-api-key" },       .{ .canonical = "llm-api-key", .word = "llm-key" },
    .{ .canonical = "llm-api-key", .word = "llm-token" },         .{ .canonical = "chat", .word = "chat" },
    .{ .canonical = "chat-dock", .word = "chat-dock" },           .{ .canonical = "open", .word = "open" },
};
pub const secret_keys = [_][]const u8{ "token", "llm-api-key" };
pub const theme_words = [_][2][]const u8{ .{ "dark", "dark" }, .{ "light", "light" }, .{ "auto", "system" }, .{ "system", "system" } };
pub const chat_docks = [_][]const u8{ "left", "right", "top", "bottom", "float" };
pub const true_words = [_][]const u8{ "true", "yes", "on" };
pub const false_words = [_][]const u8{ "false", "no", "off" };
pub const unsafe_keys = [_][]const u8{ "__proto__", "constructor", "prototype" };

pub fn oneOf(word: []const u8, list: []const []const u8) bool {
    for (list) |w| if (std.mem.eql(u8, word, w)) return true;
    return false;
}

pub fn canonicalOf(word: []const u8) ?[]const u8 {
    for (spellings) |s| if (std.mem.eql(u8, s.word, word)) return s.canonical;
    return null;
}

/// LAUNCH.scriptMaxChars: the longest script a link carries.
pub fn scriptMaxChars(a: std.mem.Allocator) usize {
    const v = std.json.parseFromSliceLeaky(std.json.Value, a, constants_json, .{}) catch return 200000;
    const launch = v.object.get("LAUNCH") orelse return 200000;
    const n = launch.object.get("scriptMaxChars") orelse return 200000;
    return @intCast(n.integer);
}

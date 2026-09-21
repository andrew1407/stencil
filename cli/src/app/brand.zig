//! The CLI's brand art colours, read from the canonical shared JSON (browser/js/config/
//! themeTokens.json `brand`, embedded at build time) — no hex is retyped here. logo.zig needs them
//! as COMPILE-TIME constants, which rules out theme.zig's runtime std.json parse, so this is a
//! comptime scan of the same bytes; tests/theme_tokens_drift_test.zig pins every triple.
const std = @import("std");

const theme_tokens_json = @embedFile("themeTokens.json");

pub const accent = rgbOf("accent"); // logo panel outline / prompt (the violet default)
pub const annotation = rgbOf("annotation"); // the signature yellow polyline
pub const panel = rgbOf("panel"); // dark app-panel fill
pub const panel_inner = rgbOf("panelInner"); // lighter inner image frame
pub const panel_grid = rgbOf("panelGrid"); // faint point outline / grid dots
pub const error_red = rgbOf("errorRed"); // the `error: ` prefix

/// The `brand` hex named by `key`, as an RGB triple. Comptime-only.
pub fn rgbOf(comptime key: []const u8) [3]u8 {
    @setEvalBranchQuota(theme_tokens_json.len * 4); // comptime scan of the whole asset
    const brand = theme_tokens_json[std.mem.indexOf(u8, theme_tokens_json, "\"brand\": {").?..];
    const needle = "\"" ++ key ++ "\": \"#";
    const at = std.mem.indexOf(u8, brand, needle).? + needle.len;
    const hex = brand[at .. at + 6];
    return .{ byteOf(hex[0..2]), byteOf(hex[2..4]), byteOf(hex[4..6]) };
}

fn byteOf(comptime s: []const u8) u8 {
    return std.fmt.parseInt(u8, s, 16) catch @compileError("themeTokens.json: bad brand hex " ++ s);
}

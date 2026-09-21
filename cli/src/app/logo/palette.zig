//! The console palette: every brand triple from themeTokens.json (via brand.zig) turned
//! into its SGR escape at compile time — no hex is spelled out twice.
const std = @import("std");
const brand = @import("../brand.zig");

// Every colour below is the brand triple from themeTokens.json (via brand.zig), turned into
// its SGR escape at compile time — no hex is spelled out twice.
pub const Ansi = struct {
    pub const reset = "\x1b[0m";
    pub const bold = "\x1b[1m";
    pub const purple = fg(brand.accent); // panel stroke (favicon border)
    pub const yellow = fg(brand.annotation); // polyline (favicon annotation)
    pub const frame_bg = bg(brand.panel); // app panel
    pub const field_bg = bg(brand.panel_inner); // inner image frame
    pub const grid = fg(brand.panel_grid); // faint point outline / grid dots
    pub const red = boldFg(brand.error_red); // `error:` prefix
};

pub fn fg(comptime rgb: [3]u8) []const u8 {
    return std.fmt.comptimePrint("\x1b[38;2;{d};{d};{d}m", .{ rgb[0], rgb[1], rgb[2] });
}
pub fn bg(comptime rgb: [3]u8) []const u8 {
    return std.fmt.comptimePrint("\x1b[48;2;{d};{d};{d}m", .{ rgb[0], rgb[1], rgb[2] });
}
pub fn boldFg(comptime rgb: [3]u8) []const u8 {
    return std.fmt.comptimePrint("\x1b[1;38;2;{d};{d};{d}m", .{ rgb[0], rgb[1], rgb[2] });
}

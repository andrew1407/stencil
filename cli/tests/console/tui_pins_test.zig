//! Golden pins for every rendered terminal surface: `--help`, the logo art, and the console listings
//! (intro, /help, /theme, /filter, /format). Each render is captured through logo.zig's sink
//! seam — the same one the full-screen console installs — and compared byte-for-byte with
//! tests/pins/<name>.<variant>.txt, SGR escapes included: they are user-visible output.
//! `STENCIL_UPDATE_PINS=1 zig build test` rewrites the goldens.
const std = @import("std");
const logo = @import("../../src/app/logo.zig");
const ui = @import("../../src/console/ui.zig");
const Session = @import("../../src/console/session.zig").Session;
const pin = @import("tui_pins_harness.zig").pin;
const testing = std.testing;

// ── the rendered surfaces ──────────────────────────────────────────────────────

fn renderUsage(_: std.mem.Allocator) anyerror!void {
    logo.usage();
}

fn renderBanner(_: std.mem.Allocator) anyerror!void {
    logo.banner();
}

fn renderIntro(_: std.mem.Allocator) anyerror!void {
    ui.intro();
}

fn renderHelp(_: std.mem.Allocator) anyerror!void {
    ui.help();
}

fn renderThemes(_: std.mem.Allocator) anyerror!void {
    ui.listThemes();
}

fn renderFilters(_: std.mem.Allocator) anyerror!void {
    ui.listFilters();
}

fn renderFormats(a: std.mem.Allocator) anyerror!void {
    var session = Session{ .gpa = a };
    defer session.deinit();
    ui.listFormats(&session); // no explicit pick: the A4 fallback is marked current
}

// ── tests ──────────────────────────────────────────────────────────────────────

test "pins: --help usage text and the logo art" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    try pin(io, "usage", renderUsage);
    try pin(io, "banner", renderBanner);
}

test "pins: console intro and /help" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    try pin(io, "intro", renderIntro);
    try pin(io, "help", renderHelp);
}

test "pins: /theme, /filter and /format listings" {
    var threaded = std.Io.Threaded.init(testing.allocator, .{});
    defer threaded.deinit();
    const io = threaded.io();
    try pin(io, "themes", renderThemes);
    try pin(io, "filters", renderFilters);
    try pin(io, "formats", renderFormats);
}

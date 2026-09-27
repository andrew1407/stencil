//! The golden-pin rig: a render captured through logo.zig's sink seam in both variants, colour and
//! NO_COLOR, under fixed UI state, compared with tests/pins/<name>.<variant>.txt — or recorded
//! there under `STENCIL_UPDATE_PINS=1` — with the first differing lines printed escapes and all.
const std = @import("std");
const logo = @import("../../src/app/logo.zig");
const theme = @import("../../src/app/theme.zig");
const ui = @import("../../src/console/ui.zig");
const testing = std.testing;

const pins_dir = "tests/pins"; // `zig build test` runs with cwd = cli/
// The terminal width the pins are rendered at. Nothing captured here wraps today, so fixing it
// explicitly makes a renderer that starts wrapping show up as a diff, not a machine-dependent pin.
const pin_cols = 100;

// Collects logo.print output through the sink seam the full-screen console installs.
const Cap = struct {
    buf: std.ArrayList(u8) = .empty,
    fn sink(ctx: *anyopaque, bytes: []const u8) void {
        const self: *Cap = @ptrCast(@alignCast(ctx));
        self.buf.appendSlice(testing.allocator, bytes) catch {};
    }
};

const Render = *const fn (std.mem.Allocator) anyerror!void;

/// Pin one render in both variants: colour forced on (escapes and all) and NO_COLOR.
pub fn pin(io: std.Io, name: []const u8, body: Render) !void {
    try pinVariant(io, name, "color", false, body);
    try pinVariant(io, name, "plain", true, body);
}

fn pinVariant(io: std.Io, name: []const u8, variant: []const u8, no_color: bool, body: Render) !void {
    var cap = Cap{};
    defer cap.buf.deinit(testing.allocator);
    logo.setSink(Cap.sink, &cap);
    defer logo.clearSink();
    defer logo.init(false, false); // module defaults for whatever runs next

    resetUi(no_color);
    try body(testing.allocator);

    var pbuf: [128]u8 = undefined;
    const path = try std.fmt.bufPrint(&pbuf, "{s}/{s}.{s}.txt", .{ pins_dir, name, variant });
    if (updating()) {
        std.Io.Dir.cwd().createDirPath(io, pins_dir) catch {};
        try std.Io.Dir.cwd().writeFile(io, .{ .sub_path = path, .data = cap.buf.items });
        return;
    }
    try compare(io, path, cap.buf.items);
}

/// Fixed UI state for every pin: default (violet) accent, no accent sentinel, colour driven
/// only by `no_color`, non-interactive.
fn resetUi(no_color: bool) void {
    logo.setAccentSentinel(false);
    logo.setAccent(theme.rgbOf(theme.default_key));
    logo.init(no_color, true); // treat stderr as a terminal, so severity colour is on too
    ui.setInteractive(false);
    ui.setAccent(theme.default_key);
}

fn updating() bool {
    const v = std.c.getenv("STENCIL_UPDATE_PINS") orelse return false;
    const s = std.mem.span(v);
    return s.len != 0 and !std.mem.eql(u8, s, "0");
}

fn compare(io: std.Io, path: []const u8, got: []const u8) !void {
    const want = std.Io.Dir.cwd().readFileAlloc(io, path, testing.allocator, .limited(1 << 20)) catch |e| {
        std.debug.print("pin '{s}' unreadable ({s}) — record it with STENCIL_UPDATE_PINS=1 zig build test\n", .{ path, @errorName(e) });
        return error.MissingPin;
    };
    defer testing.allocator.free(want);
    if (std.mem.eql(u8, want, got)) return;
    try printDiff(path, want, got);
    return error.PinMismatch;
}

/// Print the first differing lines with three lines of context, escapes shown literally
/// (\x1b, \x01) — a raw byte compare says nothing about which escape moved.
fn printDiff(path: []const u8, want: []const u8, got: []const u8) !void {
    const a = testing.allocator;
    const wl = try lines(a, want);
    defer a.free(wl);
    const gl = try lines(a, got);
    defer a.free(gl);

    var first: usize = 0;
    while (first < wl.len and first < gl.len and std.mem.eql(u8, wl[first], gl[first])) : (first += 1) {}
    std.debug.print("\npin mismatch: {s} (line {d}; STENCIL_UPDATE_PINS=1 to re-record)\n", .{ path, first + 1 });
    const from = first -| 3;
    for (from..first) |i| try printLine(a, "  ", wl[i]);
    for (first..@min(first + 3, wl.len)) |i| try printLine(a, "- ", wl[i]);
    for (first..@min(first + 3, gl.len)) |i| try printLine(a, "+ ", gl[i]);
}

fn lines(a: std.mem.Allocator, text: []const u8) ![][]const u8 {
    var out: std.ArrayList([]const u8) = .empty;
    defer out.deinit(a);
    var it = std.mem.splitScalar(u8, text, '\n');
    while (it.next()) |l| try out.append(a, l);
    return out.toOwnedSlice(a);
}

fn printLine(a: std.mem.Allocator, mark: []const u8, line: []const u8) !void {
    var out: std.ArrayList(u8) = .empty;
    defer out.deinit(a);
    for (line) |ch| {
        if (ch >= 0x20 or ch == '\t') try out.append(a, ch) else try out.print(a, "\\x{x:0>2}", .{ch});
        if (out.items.len >= pin_cols * 4) break; // a wall of escapes helps nobody
    }
    std.debug.print("{s}{s}\n", .{ mark, out.items });
}

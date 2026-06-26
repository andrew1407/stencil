//! Console logo + help text. The logo echoes browser/favicon.svg: a purple rounded
//! panel framing the signature yellow annotation polyline with point markers. Human
//! output goes to stderr so it never contaminates a piped result; colour is suppressed
//! when NO_COLOR is set or stderr is not a terminal.
const std = @import("std");

const Ansi = struct {
    const reset = "\x1b[0m";
    const bold = "\x1b[1m";
    const purple = "\x1b[38;2;124;58;237m"; // #7c3aed panel stroke (favicon border)
    const yellow = "\x1b[38;2;255;255;0m"; // #FFFF00 polyline (favicon annotation)
    const frame_bg = "\x1b[48;2;43;47;58m"; // #2b2f3a app panel
    const field_bg = "\x1b[48;2;58;63;75m"; // #3a3f4b inner image frame
    const grid = "\x1b[38;2;90;96;110m"; // faint marker outline / grid dots
};

var use_color: bool = true;

// The brand accent (logo panel outline, prompt, echoed commands). Defaults to violet
// (#7c3aed); the console's `/theme` swaps it. `accent_slice` caches its SGR escape.
var accent_rgb: [3]u8 = .{ 124, 58, 237 };
var accent_buf: [20]u8 = undefined;
var accent_slice: []const u8 = "";

fn refreshAccent() void {
    accent_slice = if (use_color)
        std.fmt.bufPrint(&accent_buf, "\x1b[38;2;{d};{d};{d}m", .{ accent_rgb[0], accent_rgb[1], accent_rgb[2] }) catch ""
    else
        "";
}

/// Enable colour unless NO_COLOR is set (the caller checks the environment).
pub fn init(no_color: bool) void {
    use_color = !no_color;
    refreshAccent();
}

/// Repaint the brand accent (logo outline, prompt, command echo) to an RGB triple.
pub fn setAccent(rgb: [3]u8) void {
    accent_rgb = rgb;
    refreshAccent();
}

/// SGR escape for the current accent, and the reset; both "" when colour is off. Used by
/// the line editor to colour the prompt and the typed command.
pub fn accentSeq() []const u8 {
    return accent_slice;
}
pub fn resetSeq() []const u8 {
    return c(Ansi.reset);
}
pub fn colorEnabled() bool {
    return use_color;
}

fn c(comptime code: []const u8) []const u8 {
    return if (use_color) code else "";
}

/// Print to stderr (the CLI's human channel).
pub fn print(comptime fmt: []const u8, args: anytype) void {
    std.debug.print(fmt, args);
}

// A larger text rendering of browser/favicon.svg, laid out to read square in a
// terminal (cells are ~2:1 tall, so the panel spans about twice as many columns as
// rows). It reproduces the icon's pieces: a purple rounded panel (the curved corners
// echo the SVG's rx="13"), the dark app panel (frame_bg) forming a margin around the
// lighter inner image frame (field_bg), and the signature yellow annotation polyline
// with a round marker (●) at each vertex.
//
// FRAME_W/FRAME_H is the lighter inner frame; the polyline is rasterised at runtime
// from the favicon's vertices (16,46)→(27,24)→(38,38)→(50,18), mapped into the frame.
// Mh/Mv is the dark app-panel margin around it; the rounded purple border is drawn
// outside that.
const FRAME_W = 14; // lighter inner frame width, in cells
const FRAME_H = 6; // lighter inner frame height, in rows
const Mh = 1; // horizontal dark app-panel margin (cells)
const Mv = 0; // vertical dark app-panel margin (rows); curve rows supply the dark cap
const PANEL_W = FRAME_W + Mh * 2; // inner width between the side borders
const BODY_H = FRAME_H + Mv * 2; // inner height between the top/bottom borders

const Pt = struct { col: usize, row: usize };
// Favicon vertices mapped into the FRAME_W×FRAME_H cell grid.
const verts = [_]Pt{
    .{ .col = 1, .row = 4 }, // (16,46)
    .{ .col = 5, .row = 2 }, // (27,24)
    .{ .col = 8, .row = 3 }, // (38,38)
    .{ .col = 12, .row = 1 }, // (50,18)
};

// Glyph codes laid into the rasterised frame.
const G_SPACE = 0;
const G_UP = 1; // ╱ (segment rising left→right)
const G_DOWN = 2; // ╲ (segment falling left→right)
const G_MARK = 3; // ● (polyline vertex)

fn glyph(code: u8) []const u8 {
    return switch (code) {
        G_UP => "╱",
        G_DOWN => "╲",
        G_MARK => "●",
        else => " ",
    };
}

// Rasterise the polyline into a frame-sized grid: straight strokes between vertices
// (slope picks ╱ or ╲), with a ● dropped on each vertex.
fn rasterise() [FRAME_H][FRAME_W]u8 {
    var g = std.mem.zeroes([FRAME_H][FRAME_W]u8);
    for (0..verts.len - 1) |s| {
        const a = verts[s];
        const z = verts[s + 1];
        const stroke: u8 = if (z.row < a.row) G_UP else G_DOWN;
        const dc = @as(i32, @intCast(z.col)) - @as(i32, @intCast(a.col));
        const dr = @as(i32, @intCast(z.row)) - @as(i32, @intCast(a.row));
        const steps = @max(@abs(dc), @abs(dr));
        var i: i32 = 1;
        while (i < steps) : (i += 1) {
            const t = @as(f64, @floatFromInt(i)) / @as(f64, @floatFromInt(steps));
            const cf = @as(f64, @floatFromInt(a.col)) + @as(f64, @floatFromInt(dc)) * t;
            const rf = @as(f64, @floatFromInt(a.row)) + @as(f64, @floatFromInt(dr)) * t;
            const cc: usize = @intFromFloat(@round(cf));
            const rr: usize = @intFromFloat(@round(rf));
            if (g[rr][cc] == G_SPACE) g[rr][cc] = stroke;
        }
    }
    for (verts) |v| g[v.row][v.col] = G_MARK;
    return g;
}

fn spaces(n: usize) void {
    var i: usize = 0;
    while (i < n) : (i += 1) print(" ", .{});
}

fn rule(comptime g: []const u8, width: usize) void {
    var i: usize = 0;
    while (i < width) : (i += 1) print(g, .{});
}

pub fn banner() void {
    const p = accent_slice; // brand accent (violet by default) — themeable via /theme
    const y = c(Ansi.yellow);
    const b = c(Ansi.bold);
    const r = c(Ansi.reset);
    const fbg = c(Ansi.frame_bg);
    const ibg = c(Ansi.field_bg);
    const grid = rasterise();

    print("\n", .{});
    // Rounded top: an inset ╭──╮ with ╱ ╲ curving out to the full-width sides — a text
    // approximation of the SVG's rounded corners (rx="13").
    print("  {s} ╭", .{p});
    rule("─", PANEL_W - 2);
    print("╮{s}\n", .{r});
    print("  {s}╱{s}", .{ p, fbg }); // dark app-panel fills the curve, no black gap
    spaces(PANEL_W);
    print("{s}{s}╲{s}\n", .{ r, p, r });

    // Inner rows: side border, dark margin, lighter image frame, dark margin, side
    // border. The wordmark sits to the right of the panel, vertically centred.
    const label_row = BODY_H / 2;
    var row_idx: usize = 0;
    while (row_idx < BODY_H) : (row_idx += 1) {
        print("  {s}│{s}{s}", .{ p, r, fbg }); // left border, then dark app panel
        spaces(Mh); // left dark margin
        if (row_idx >= Mv and row_idx < Mv + FRAME_H) {
            const fr = row_idx - Mv;
            print("{s}{s}", .{ ibg, y }); // lighter image frame, yellow annotation
            for (grid[fr]) |code| print("{s}", .{glyph(code)});
            print("{s}", .{fbg}); // back to dark for the right margin
        } else {
            spaces(FRAME_W); // dark margin row (top / bottom of the inner frame)
        }
        spaces(Mh); // right dark margin
        print("{s}{s}│{s}", .{ r, p, r }); // right border on default bg
        if (row_idx == label_row) print("   {s}S T E N C I L{s}", .{ b, r });
        print("\n", .{});
    }

    print("  {s}╲{s}", .{ p, fbg }); // dark app-panel fills the curve, no black gap
    spaces(PANEL_W);
    print("{s}{s}╱{s}\n", .{ r, p, r });
    print("  {s} ╰", .{p});
    rule("─", PANEL_W - 2);
    print("╯{s}\n\n", .{r});
}

pub fn usage() void {
    const b = c(Ansi.bold);
    const r = c(Ansi.reset);
    print(
        \\{s}Usage{s}
        \\  stencil [options] <output>
        \\
        \\{s}Source (choose one){s}
        \\  -i, --input <path|url>     Image or video file/URL to load
        \\      --blank <w h [color]>  Create a blank page (color: name or #hex, default white)
        \\
        \\{s}Options{s}
        \\  -f, --frame <n>            Video frame index to grab (default 0)
        \\  -c, --crop "<spec>"        Crop, e.g. "x1=10% x2=90% y1=10% y2=90%"
        \\                             (units: px, cm, mm, in, %, or a bare pixel delta)
        \\      --album                With one crop axis, derive the other (landscape)
        \\  -r, --rotate <int>         Rotate int*90 deg (e.g. -1 = -90, 3 = 270)
        \\  -l, --layout <path|url>    Layout JSON to draw onto the image
        \\      --filter <f>           Apply bw | sepia | <color>; overrides the layout filter
        \\      --console              Interactive console: /upload, /crop, /rotate, /save, ...
        \\  -h, --help                 Show this help
        \\
        \\{s}Output{s}
        \\  <output>                   Result path. A missing extension is filled in from
        \\                             the input/format (png, jpg, bmp, tga).
        \\
        \\{s}Examples{s}
        \\  stencil -i photo.jpg -c "x1=10% x2=90% y1=10% y2=90%" -r 1 out.png
        \\  stencil --blank 800 600 red --layout notes.json --filter sepia out
        \\  stencil -i clip.mp4 -f 24 frame.png
        \\  stencil --console          (then: /upload photo.png / /crop ... / /rotate 1 / /save out.png)
        \\
    , .{
        b, r, b, r, b, r, b, r, b, r,
    });
}

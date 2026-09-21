//! The console logo: a text rendering of browser/favicon.svg — a purple rounded panel
//! framing the signature yellow annotation polyline, rasterised from the favicon's own
//! vertices at two sizes (the small one is the pressed-button frame).
const std = @import("std");
const logo = @import("../logo.zig");
const palette = @import("palette.zig");

const Ansi = palette.Ansi;
const print = logo.print;
const c = logo.colorSeq;
const accentSeq = logo.accentSeq;
const accentReal = logo.accentReal;
const resetSeq = logo.resetSeq;

// A larger text rendering of browser/favicon.svg, laid out to read square in a terminal (cells are
// ~2:1 tall). FRAME_W/H is the lighter inner frame; Mh/Mv the dark app-panel margin around it.
const FRAME_W = 14; // lighter inner frame width, in cells
const FRAME_H = 6; // lighter inner frame height, in rows
const Mh = 1; // horizontal dark app-panel margin (cells)
const Mv = 0; // vertical dark app-panel margin (rows); curve rows supply the dark cap
const PANEL_W = FRAME_W + Mh * 2; // inner width between the side borders
const BODY_H = FRAME_H + Mv * 2; // inner height between the top/bottom borders

const Pt = struct { col: usize, row: usize };
// Favicon vertices mapped into the FRAME_W×FRAME_H cell grid. Cells are ~2:1 tall, so the S is
// snapped to the grid rather than scaled: the bars land ON a row and the joins step one row at a time.
const verts = [_]Pt{
    .{ .col = 11, .row = 0 }, // top-right end
    .{ .col = 3, .row = 0 }, // top bar, running left
    .{ .col = 1, .row = 1 }, // down the left side
    .{ .col = 3, .row = 2 }, // back onto the middle row
    .{ .col = 10, .row = 2 }, // middle bar, running right
    .{ .col = 12, .row = 3 }, // down the right side
    .{ .col = 11, .row = 5 }, // …to the bottom row
    .{ .col = 2, .row = 5 }, // bottom bar, running left
};

// The SMALL mark: the same S snapped into a 10×5 grid, for the pressed-logo frame — panel, dark
// margin and artwork shrink together, so the click reads as a button going down.
const FRAME_W_S = 10;
const FRAME_H_S = 5;
const verts_small = [_]Pt{
    .{ .col = 8, .row = 0 }, // top-right end
    .{ .col = 2, .row = 0 }, // top bar, running left
    .{ .col = 1, .row = 1 }, // down the left side
    .{ .col = 2, .row = 2 }, // back onto the middle row
    .{ .col = 7, .row = 2 }, // middle bar, running right
    .{ .col = 8, .row = 3 }, // down the right side
    .{ .col = 7, .row = 4 }, // …to the bottom row
    .{ .col = 1, .row = 4 }, // bottom bar, running left — one cell wider than the middle one,
}; // so the three bars stagger and still read as an S at half size

// Glyph codes laid into the rasterised frame.
const G_SPACE = 0;
const G_UP = 1; // ╱ (segment rising left→right)
const G_DOWN = 2; // ╲ (segment falling left→right)
const G_MARK = 3; // ● (polyline vertex)
const G_FLAT = 4; // ─ (segment level across a row — the S's bars)

fn glyph(code: u8) []const u8 {
    return switch (code) {
        G_UP => "╱",
        G_DOWN => "╲",
        G_FLAT => "─",
        G_MARK => "●",
        else => " ",
    };
}

// Rasterise a polyline into a W×H grid: straight strokes between vertices (slope picks ╱ or ╲), a ● on
// each vertex. The small mark is hand-snapped, not scaled: 14×6 → 10×5 collapses bars onto joins.
fn rasterise(comptime W: usize, comptime H: usize, comptime vs: []const Pt) [H][W]u8 {
    var g = std.mem.zeroes([H][W]u8);
    for (0..vs.len - 1) |s| {
        const a = vs[s];
        const z = vs[s + 1];
        // The glyph must follow the segment's real slope, which needs BOTH deltas: the S runs right→left
        // across its bars, so a down-LEFT join is ╱, not ╲. A level run gets its own glyph.
        const down_right = (z.row > a.row) == (z.col > a.col);
        const stroke: u8 = if (z.row == a.row) G_FLAT else if (down_right) G_DOWN else G_UP;
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
    for (vs) |v| g[v.row][v.col] = G_MARK;
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
    emitBanner(false);
}

/// The logo at its pressed size: the whole icon redrawn about two cells smaller on each side (18×10 →
/// 14×7) around the smaller mark. The wordmark is not part of it — the press moves the icon only.
pub fn bannerCompact() void {
    emitBanner(true);
}

fn emitBanner(comptime compact: bool) void {
    const p = accentReal(); // brand accent (violet by default) — themeable via /theme
    const y = c(Ansi.yellow);
    const b = c(Ansi.bold);
    const r = c(Ansi.reset);
    const fbg = c(Ansi.frame_bg);
    const ibg = c(Ansi.field_bg);

    // Both sizes are the same drawing at two scales; only the frame constants change. The pressed one is
    // indented further to stay centred, and drops the curved caps (no room for them at 7 rows).
    const fw = if (compact) FRAME_W_S else FRAME_W;
    const fh = if (compact) FRAME_H_S else FRAME_H;
    const grid = rasterise(fw, fh, if (compact) &verts_small else &verts);
    const panel_w = fw + Mh * 2;
    const body_h = fh + Mv * 2;
    const indent: usize = if (compact) 4 else 2;

    print("\n", .{});
    // Rounded top: an inset ╭──╮ with ╱ ╲ curving out to the full-width sides — a text
    // approximation of the SVG's rounded corners (rx="13").
    spaces(indent);
    print("{s}{s}╭", .{ p, if (compact) "" else " " });
    rule("─", if (compact) panel_w else panel_w - 2);
    print("╮{s}\n", .{r});
    if (!compact) {
        spaces(indent);
        print("{s}╱{s}", .{ p, fbg }); // dark app-panel fills the curve, no black gap
        spaces(panel_w);
        print("{s}{s}╲{s}\n", .{ r, p, r });
    }

    // Inner rows: side border, dark margin, lighter image frame, dark margin, side
    // border. The wordmark sits to the right of the panel, vertically centred.
    const label_row = body_h / 2;
    var row_idx: usize = 0;
    while (row_idx < body_h) : (row_idx += 1) {
        spaces(indent);
        print("{s}│{s}{s}", .{ p, r, fbg }); // left border, then dark app panel
        spaces(Mh); // left dark margin
        if (row_idx >= Mv and row_idx < Mv + fh) {
            const fr = row_idx - Mv;
            print("{s}{s}", .{ ibg, y }); // lighter image frame, yellow annotation
            for (grid[fr]) |code| print("{s}", .{glyph(code)});
            print("{s}", .{fbg}); // back to dark for the right margin
        } else {
            spaces(fw); // dark margin row (top / bottom of the inner frame)
        }
        spaces(Mh); // right dark margin
        print("{s}{s}│{s}", .{ r, p, r }); // right border on default bg
        if (!compact and row_idx == label_row) print("   {s}S T E N C I L{s}", .{ b, r });
        print("\n", .{});
    }

    if (!compact) {
        spaces(indent);
        print("{s}╲{s}", .{ p, fbg }); // dark app-panel fills the curve, no black gap
        spaces(panel_w);
        print("{s}{s}╱{s}\n", .{ r, p, r });
    }
    spaces(indent);
    print("{s}{s}╰", .{ p, if (compact) "" else " " });
    rule("─", if (compact) panel_w else panel_w - 2);
    print("╯{s}\n\n", .{r});
}

//! Session-wide look and history steps: `/undo`/`/redo` acks, `/reset`, `/drop`, `/theme`
//! (and the logo-click accent cycle), `/mouse` and `/reveal-speed`.
const std = @import("std");
const logo = @import("../../logo.zig");
const core = @import("../../core.zig");
const theme = @import("../../theme.zig");
const msg = @import("../../messages.zig");
const ui = @import("../ui.zig");
const screen = @import("../screen.zig");
const Session = @import("../session.zig").Session;

pub fn doStep(session: *Session, moved: bool, ok: []const u8, none: []const u8) void {
    if (!session.hasImage()) return ui.noImage();
    if (moved) ui.ack(session, ok) else logo.print("{s}\n", .{none});
}

pub fn doReset(session: *Session) void {
    if (!session.hasImage()) return ui.noImage();
    session.revert();
    ui.redraw(session);
    logo.print(msg.reset_done, .{});
}

pub fn doDrop(session: *Session) void {
    if (!session.hasImage()) return ui.noImage();
    session.clearAll();
    ui.redraw(session); // header now reads "(none)"
}

pub fn doTheme(session: *Session, arg: []const u8) void {
    if (arg.len == 0) return ui.listThemes();

    // A named preset, with 'default' as an alias for the default accent (violet).
    const key = if (std.ascii.eqlIgnoreCase(arg, "default")) theme.default_key else arg;
    if (theme.find(key)) |a| {
        applyAccent(session, a.rgb, a.key, a.hex, true);
        return;
    }

    // Otherwise accept any colour the core understands — a '#rrggbb' hex or a CSS name.
    if (core.parseColor(core.zstr(arg) orelse "")) |c| {
        var hexbuf: [8]u8 = undefined;
        const hex = std.fmt.bufPrint(&hexbuf, "#{x:0>2}{x:0>2}{x:0>2}", .{ c.r, c.g, c.b }) catch "#??????";
        applyAccent(session, .{ c.r, c.g, c.b }, hex, hex, true);
        return;
    }

    logo.err(msg.unknown_theme, .{arg});
}

// Repaint everything in a new accent: the logo's RGB, the stored label and the screen.
// `announce` prints the "theme set to …" line — typed `/theme` does, logo clicks stay silent.
fn applyAccent(session: *Session, rgb: [3]u8, label: []const u8, hex: []const u8, announce: bool) void {
    logo.setAccent(rgb);
    ui.setAccent(label);
    // In full-screen mode recapture the pinned logo header in the new accent (keeping the
    // scrollback); otherwise fall back to the classic clear-and-reprint.
    if (screen.current()) |s| s.onThemeChanged() else ui.redraw(session);
    if (announce) logo.print(msg.theme_set, .{ label, hex });
}

/// Advance to the next accent preset (wrapping), or reset to the default when a custom colour
/// is active — the single-click-on-logo behaviour, mirroring the browser (accents.js). Silent.
pub fn cycleTheme(session: *Session) void {
    const key = screen.nextAccentKey(ui.currentAccentKey());
    const a = theme.find(key) orelse theme.accents()[0];
    applyAccent(session, a.rgb, a.key, a.hex, false);
}

/// Set a random vivid custom colour (outside the preset list) — the double-click-on-logo
/// behaviour, the terminal stand-in for the browser logo's colour picker. Silent. `seed` varies
/// per click (the caller passes the click time) so each double-click yields a different hue.
pub fn randomCustomTheme(session: *Session, seed: u64) void {
    var prng = std.Random.DefaultPrng.init(seed);
    const h = @as(f64, @floatFromInt(prng.random().intRangeLessThan(u16, 0, 360)));
    const rgb = theme.hsvToRgb(h, 0.7, 0.95); // always vivid + readable, essentially never a preset
    var hexbuf: [8]u8 = undefined;
    const hex = std.fmt.bufPrint(&hexbuf, "#{x:0>2}{x:0>2}{x:0>2}", .{ rgb[0], rgb[1], rgb[2] }) catch "#??????";
    applyAccent(session, rgb, hex, hex, false);
}

/// `/mouse [on|off]` (bare toggles) — mouse reporting in full-screen mode: OFF hands the
/// mouse back for native select/copy, ON re-enables logo clicks + wheel + drag-selection.
/// No-op (with a note) outside full-screen.
pub fn doMouse(session: *Session, arg: []const u8) void {
    _ = session;
    const s = screen.current() orelse {
        logo.print(msg.mouse_full_screen_only, .{});
        return;
    };
    const on = if (arg.len == 0)
        !s.mouseOn()
    else if (std.ascii.eqlIgnoreCase(arg, "on"))
        true
    else if (std.ascii.eqlIgnoreCase(arg, "off"))
        false
    else {
        logo.print(msg.mouse_usage, .{});
        return;
    };
    s.setMouse(on);
    if (on)
        logo.print(msg.mouse_on, .{})
    else
        logo.print(msg.mouse_off, .{});
}

/// `/reveal-speed [speed]` — how fast new output reveals, 0.01 … 1 (bare shows; 1 =
/// instant; `off`/`on` name the two ends; STENCIL_CONSOLE_REVEAL_SPEED is the per-session
/// default). No-op with a note outside full-screen, the only mode painting its own output.
pub fn doRevealSpeed(session: *Session, arg: []const u8) void {
    _ = session;
    const s = screen.current() orelse {
        logo.print(msg.reveal_full_screen_only, .{});
        return;
    };
    if (arg.len == 0) {
        report(s.revealSpeed(), true);
        return;
    }
    const want: f64 = if (std.ascii.eqlIgnoreCase(arg, "off"))
        screen.speed_max
    else if (std.ascii.eqlIgnoreCase(arg, "on"))
        screen.speed_default
    else
        screen.parseRevealSpeed(arg) orelse {
            logo.print(msg.reveal_usage, .{ screen.speed_min, screen.speed_max, screen.speed_min, screen.speed_max, screen.speed_default });
            return;
        };
    s.setRevealSpeed(want);
    report(s.revealSpeed(), false);
}

/// One line describing the current reveal speed, shown or set. Deliberately terse: it is
/// itself revealed at the speed it names, so it demonstrates the setting.
fn report(speed: f64, showing: bool) void {
    const verb = if (showing) "reveal speed is" else "reveal speed";
    if (speed >= screen.speed_max) {
        logo.print(msg.reveal_instant, .{ verb, speed });
        return;
    }
    logo.print("{s} {d}\n", .{ verb, speed });
}

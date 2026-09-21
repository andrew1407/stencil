//! `/format` and `/formula`: the page format the header and `/blank` default to, and the
//! coordinate-transform formulas that ride the saved layout (validated by the core parser).
const std = @import("std");
const logo = @import("../../app/logo.zig");
const core = @import("../../core.zig");
const msg = @import("../../app/messages.zig");
const ui = @import("../ui.zig");
const Session = @import("../session.zig").Session;

pub fn printFormula(session: *Session) void {
    const fx = if (session.formula_x.len != 0) session.formula_x else "(identity)";
    const fy = if (session.formula_y.len != 0) session.formula_y else "(identity)";
    logo.print(msg.formulas_state, .{ if (session.allow_formulas) "on" else "off", fx, fy });
}

/// `/formula [x|y <expr> | on | off | clear]` — the coordinate-transform formulas riding the saved
/// layout, validated with the shared parser. True only when the state changed, so only then a sync.
pub fn doFormula(session: *Session, arg: []const u8) bool {
    const trimmed = std.mem.trim(u8, arg, " \t");
    if (trimmed.len == 0) {
        printFormula(session);
        return false;
    }
    // Split into the sub-command word + the remainder (the expression, which may have spaces).
    var i: usize = 0;
    while (i < trimmed.len and trimmed[i] != ' ' and trimmed[i] != '\t') : (i += 1) {}
    const sub = trimmed[0..i];
    const expr = std.mem.trim(u8, trimmed[i..], " \t");
    const eq = std.ascii.eqlIgnoreCase;
    if (eq(sub, "on")) {
        session.setAllowFormulas(true);
        printFormula(session);
    } else if (eq(sub, "off")) {
        session.setAllowFormulas(false);
        logo.print(msg.formulas_off, .{});
    } else if (eq(sub, "clear") or eq(sub, "none")) {
        session.clearFormulas();
        logo.print(msg.formulas_cleared, .{});
    } else if (eq(sub, "x") or eq(sub, "y")) {
        const axis: u8 = if (eq(sub, "y")) 'y' else 'x';
        const ok = session.setFormula(axis, expr) catch {
            logo.err(msg.out_of_memory, .{});
            return false;
        };
        if (!ok) {
            logo.err(msg.invalid_formula, .{ axis, expr });
            return false;
        }
        printFormula(session);
    } else {
        logo.print(msg.formula_usage, .{});
        return false;
    }
    return true;
}

/// `/format [name | custom <w> <h>]` — show or set the session's page format (custom takes cm dims).
/// Drives the header label, the saved `pageSize` and the `/blank` default. True only on a change.
pub fn doFormat(session: *Session, arg: []const u8) bool {
    const trimmed = std.mem.trim(u8, arg, " \t");
    if (trimmed.len == 0) {
        ui.listFormats(session);
        return false;
    }

    var it = std.mem.tokenizeAny(u8, trimmed, " \t");
    const head = it.next().?;
    if (std.ascii.eqlIgnoreCase(head, "custom")) {
        const w = parseCmDim(it.next());
        const h = parseCmDim(it.next());
        if (w == null or h == null or it.next() != null) {
            logo.err(msg.format_custom_needs_dims, .{});
            return false;
        }
        session.setPageSize("custom") catch return false;
        session.custom_page_w = w.?;
        session.custom_page_h = h.?;
        logo.print(msg.format_set_custom, .{ w.?, h.? });
        return true;
    }

    const name = core.canonicalPageFormat(head) orelse {
        logo.err(msg.unknown_page_format, .{head});
        return false;
    };
    if (it.next() != null) {
        logo.err(msg.format_takes_one_name, .{});
        return false;
    }
    const p = core.namedPageSize(core.zstr(name) orelse "") orelse return false;
    session.setPageSize(name) catch return false;
    logo.print(msg.format_set, .{ name, p.w, p.h });
    return true;
}

/// A `/format custom` dimension token: cm as a positive float within the shared
/// custom-page range (0.1–500 cm, mirroring the browser/desktop inputs).
fn parseCmDim(tok: ?[]const u8) ?f64 {
    const t = tok orelse return null;
    const v = std.fmt.parseFloat(f64, t) catch return null;
    if (!(v >= 0.1 and v <= 500)) return null; // also rejects NaN
    return v;
}

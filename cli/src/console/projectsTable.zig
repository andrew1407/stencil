//! The `/projects` listing table: gathering per-server project rows and rendering
//! them as an aligned, colour-aware columnar table.
const std = @import("std");
const server = @import("../serverClient.zig");
const logo = @import("../logo.zig");
const theme = @import("../theme.zig");
const project = @import("../project.zig");

/// One rendered project row; all fields owned so rows outlive the per-server lists they came
/// from. `color` is the project's custom name colour ("" = none → paint in the theme accent);
/// `description` is the free-text caption ("" = none → shown as a trailing dimmed note).
pub const ProjectRow = struct { name: []u8, size: []u8, created: []u8, expires: []u8, changed: []u8, color: []u8, description: []u8, server: []const u8 };

pub fn freeRows(gpa: std.mem.Allocator, rows: *std.ArrayList(ProjectRow)) void {
    for (rows.items) |r| {
        gpa.free(r.name);
        gpa.free(r.size);
        gpa.free(r.created);
        gpa.free(r.expires);
        gpa.free(r.changed);
        gpa.free(r.color);
        gpa.free(r.description);
    }
    rows.deinit(gpa);
}

/// Fetch one server's projects and append a rendered row per project. A network/listing failure
/// is reported and skipped (other servers still list); only allocation errors propagate.
pub fn gatherRows(gpa: std.mem.Allocator, rows: *std.ArrayList(ProjectRow), client: *server.Client, now: i64, multi: bool) !void {
    const items = client.listProjectInfos() catch |e| {
        logo.err("could not list projects on {s} ({s})\n", .{ client.base, @errorName(e) });
        return;
    };
    defer server.freeProjectList(gpa, items);
    for (items) |p| {
        const name = try gpa.dupe(u8, p.name);
        errdefer gpa.free(name);
        // Some projects have no stored dimensions (e.g. never rendered) — show "-", not "0x0".
        const size = if (p.w == 0 and p.h == 0) try gpa.dupe(u8, "-") else try std.fmt.allocPrint(gpa, "{d}x{d}", .{ p.w, p.h });
        errdefer gpa.free(size);
        var tb: [32]u8 = undefined;
        const changed = try gpa.dupe(u8, server.formatAgo(&tb, now, p.updated_at));
        errdefer gpa.free(changed);
        // Created date, shown relatively like CHANGED (reuses tb after `changed`
        // is already its own allocation).
        const created = try gpa.dupe(u8, server.formatAgo(&tb, now, p.created_at));
        errdefer gpa.free(created);
        // Expiry, shown forward-looking ("in 3d" / "expired" / "never"), next to CREATED.
        const expires = try gpa.dupe(u8, server.formatUntil(&tb, now, p.expires_at));
        errdefer gpa.free(expires);
        const color = try gpa.dupe(u8, p.color);
        errdefer gpa.free(color);
        const description = try gpa.dupe(u8, p.description);
        errdefer gpa.free(description);
        try rows.append(gpa, .{ .name = name, .size = size, .created = created, .expires = expires, .changed = changed, .color = color, .description = description, .server = if (multi) client.base else "" });
    }
}

/// Render the gathered rows as a left-aligned columnar table (2-space indent, 2-space gaps).
pub fn renderTable(gpa: std.mem.Allocator, rows: []const ProjectRow, multi: bool) void {
    var nw: usize = "NAME".len;
    var sw: usize = "SIZE".len;
    var crw: usize = "CREATED".len;
    var erw: usize = "EXPIRES".len;
    var cw: usize = "CHANGED".len;
    for (rows) |r| {
        nw = @max(nw, r.name.len);
        sw = @max(sw, r.size.len);
        crw = @max(crw, r.created.len);
        erw = @max(erw, r.expires.len);
        cw = @max(cw, r.changed.len);
    }
    printRow(gpa, "NAME", "", nw, "SIZE", sw, "CREATED", crw, "EXPIRES", erw, "CHANGED", cw, if (multi) "SERVER" else null, ""); // header: no colour/description
    for (rows) |r| {
        var buf: [20]u8 = undefined;
        printRow(gpa, r.name, theme.nameSeq(r.color, &buf), nw, r.size, sw, r.created, crw, r.expires, erw, r.changed, cw, if (multi) r.server else null, r.description);
    }
}

/// Print one table row, padding each non-final column to its width. `name_seq` colours the NAME
/// column ("" = plain); a non-empty `desc` is appended as a trailing dimmed note (truncated).
/// Best-effort.
fn printRow(gpa: std.mem.Allocator, name: []const u8, name_seq: []const u8, nw: usize, size: []const u8, sw: usize, created: []const u8, crw: usize, expires: []const u8, erw: usize, changed: []const u8, cw: usize, srv: ?[]const u8, desc: []const u8) void {
    var line: std.ArrayList(u8) = .empty;
    defer line.deinit(gpa);
    appendCol(gpa, &line, "  ", 0); // 2-space indent (no padding)
    appendName(gpa, &line, name, name_seq, nw);
    appendCol(gpa, &line, size, sw);
    appendCol(gpa, &line, created, crw);
    appendCol(gpa, &line, expires, erw);
    if (srv) |s| {
        appendCol(gpa, &line, changed, cw);
        appendCol(gpa, &line, s, 0); // final column, no trailing pad
    } else {
        appendCol(gpa, &line, changed, 0); // final column
    }
    appendDesc(gpa, &line, desc); // trailing "— <caption>" note, when present
    logo.print("{s}\n", .{line.items});
}

/// Append a description as a trailing dimmed "— <caption>" note, truncated to ~48 bytes
/// (backing off any partial UTF-8 codepoint). No-op when empty. Best-effort.
fn appendDesc(gpa: std.mem.Allocator, line: *std.ArrayList(u8), desc: []const u8) void {
    if (desc.len == 0) return;
    const on = logo.colorEnabled();
    line.appendSlice(gpa, "  ") catch return;
    if (on) line.appendSlice(gpa, "\x1b[2m") catch {}; // faint
    line.appendSlice(gpa, "— ") catch return;
    if (desc.len > 48) {
        // Back off from byte 48 to a codepoint boundary (skip UTF-8 continuation bytes 0b10xxxxxx).
        var end: usize = 48;
        while (end > 0 and (desc[end] & 0xC0) == 0x80) : (end -= 1) {}
        line.appendSlice(gpa, desc[0..end]) catch return;
        line.appendSlice(gpa, "…") catch {};
    } else line.appendSlice(gpa, desc) catch return;
    if (on) line.appendSlice(gpa, logo.resetSeq()) catch {};
}

/// Append `text`, then (when width > 0) pad with spaces to `width` plus a 2-space column gap.
/// The final column passes width 0 to skip trailing padding. Best-effort.
fn appendCol(gpa: std.mem.Allocator, line: *std.ArrayList(u8), text: []const u8, width: usize) void {
    line.appendSlice(gpa, text) catch return;
    if (width == 0) return;
    var i = text.len;
    while (i < width + 2) : (i += 1) line.append(gpa, ' ') catch return;
}

/// Like appendCol for the NAME column, wrapping the (visible) name in `seq`…reset when a colour
/// is given. Padding is computed from the VISIBLE name length — the SGR escapes have zero
/// display width — so the columns stay aligned. Best-effort.
fn appendName(gpa: std.mem.Allocator, line: *std.ArrayList(u8), name: []const u8, seq: []const u8, width: usize) void {
    const on = seq.len != 0;
    if (on) line.appendSlice(gpa, seq) catch {};
    line.appendSlice(gpa, name) catch return;
    if (on) line.appendSlice(gpa, logo.resetSeq()) catch {};
    var i = name.len;
    while (i < width + 2) : (i += 1) line.append(gpa, ' ') catch return;
}

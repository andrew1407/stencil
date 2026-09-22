//! The structured edit state — crop, rotation, filter, drawn lines, page format and the
//! coordinate formulas — and the browser-compatible layout JSON it serializes to.
const Session = @import("../session.zig").Session;
const session_mod = @import("../session.zig");
const jsonStr = session_mod.jsonStr;
const jsonInt = session_mod.jsonInt;
const jsonNum = session_mod.jsonNum;
const std = @import("std");
const server = @import("../../server/client.zig");
const core = @import("../../core.zig");
const pipeline = @import("../../pipeline.zig");
const page_mod = @import("../../media/page.zig");
const EditState = @import("../session.zig").EditState;
const clampRect = @import("../session.zig").clampRect;
const rotateRectQuarters = @import("../session.zig").rotateRectQuarters;
const extractLinesJson = @import("../session.zig").extractLinesJson;
const mergeLinesJson = @import("../session.zig").mergeLinesJson;
const parseLayoutInto = @import("../session.zig").parseLayoutInto;

// editing ops (each pushes a snapshot + rebuilds)

/// Rotate by `n` quarter-turns (clockwise). The crop rect rides along into the new space.
pub fn applyRotate(self: *Session, n: i32) !void {
    const cur = self.state();
    var next = try cur.dupe(self.gpa);
    errdefer next.deinit(self.gpa);
    if (next.crop) |cr| {
        const orig = self.original.?;
        const dims = core.rotatedDims(@intCast(orig.width), @intCast(orig.height), cur.rotation);
        next.crop = rotateRectQuarters(cr, dims.w, dims.h, n);
    }
    next.rotation = core.normalizeQuarters(cur.rotation + n);
    try self.pushState(next);
}

/// Crop to `rect` (given in CURRENT-view pixels); composes into rotated-original space.
pub fn applyCrop(self: *Session, rect: core.Rect) !void {
    const cur = self.state();
    var next = try cur.dupe(self.gpa);
    errdefer next.deinit(self.gpa);
    // The view is rotate(original) cropped to `cur.crop`; a sub-rect maps back by its origin.
    const base_x: i32 = if (cur.crop) |c| c.x else 0;
    const base_y: i32 = if (cur.crop) |c| c.y else 0;
    const orig = self.original.?;
    const dims = core.rotatedDims(@intCast(orig.width), @intCast(orig.height), cur.rotation);
    next.crop = clampRect(.{ .x = base_x + rect.x, .y = base_y + rect.y, .w = rect.w, .h = rect.h }, dims.w, dims.h);
    try self.pushState(next);
}

/// Set the image filter (mode "none"|"bw"|"sepia"|"custom"; color is the custom hex).
pub fn setFilter(self: *Session, mode: []const u8, color: []const u8) !void {
    const cur = self.state();
    var next = try cur.dupe(self.gpa);
    errdefer next.deinit(self.gpa);
    if (next.filter_mode.len != 0) self.gpa.free(next.filter_mode);
    if (next.filter_color.len != 0) self.gpa.free(next.filter_color);
    next.filter_mode = try self.gpa.dupe(u8, mode);
    next.filter_color = try self.gpa.dupe(u8, color);
    try self.pushState(next);
}

/// Append the lines from a layout JSON document to the drawing.
pub fn addLines(self: *Session, layout_bytes: []const u8) !void {
    const add = try extractLinesJson(self.gpa, layout_bytes);
    defer self.gpa.free(add);
    const cur = self.state();
    var next = try cur.dupe(self.gpa);
    errdefer next.deinit(self.gpa);
    const merged = try mergeLinesJson(self.gpa, cur.lines(), add);
    if (next.lines_json.len != 0) self.gpa.free(next.lines_json);
    next.lines_json = merged;
    try self.pushState(next);
}

/// Replace the drawing's lines with those from a layout JSON document — the
/// counterpart to `addLines` (which appends), for `apply <src> replace`.
pub fn setLines(self: *Session, layout_bytes: []const u8) !void {
    const add = try extractLinesJson(self.gpa, layout_bytes);
    defer self.gpa.free(add);
    const cur = self.state();
    var next = try cur.dupe(self.gpa);
    errdefer next.deinit(self.gpa);
    const only = try self.gpa.dupe(u8, add);
    if (next.lines_json.len != 0) self.gpa.free(next.lines_json);
    next.lines_json = only;
    try self.pushState(next);
}

/// Adopt a server project's stored layout into the pristine state (used right after a
/// fetch/pull loads the original), so the view shows the peer's crop/rotation/filter/lines.
pub fn adoptServerLayout(self: *Session, layout_bytes: []const u8) !void {
    var st = EditState{};
    parseLayoutInto(self.gpa, layout_bytes, &st) catch {}; // partial parse still yields a valid st
    // Replace history[0] (we are right after loadImage, so cursor == 0). st is moved in.
    self.history.items[0].deinit(self.gpa);
    self.history.items[0] = st;
    self.cursor = 0;
    self.adoptLayoutMeta(layout_bytes); // page format + formulas (project-level, round-tripped)
    self.rebuild() catch {};
}

/// Parse the page format + x/y formulas out of a fetched layout (best-effort; cleared on miss).
pub fn adoptLayoutMeta(self: *Session, layout_bytes: []const u8) void {
    self.clearFormat();
    var parsed = std.json.parseFromSlice(std.json.Value, self.gpa, layout_bytes, .{}) catch return;
    defer parsed.deinit();
    if (parsed.value != .object) return;
    const obj = parsed.value.object;
    if (jsonStr(obj, "pageSize")) |ps| self.page_size = self.gpa.dupe(u8, ps) catch &.{};
    self.custom_page_w = jsonNum(obj, "customPageWidth");
    self.custom_page_h = jsonNum(obj, "customPageHeight");
    if (obj.get("allowFormulas")) |v| {
        if (v == .bool) self.allow_formulas = v.bool;
    }
    if (jsonStr(obj, "formulaX")) |fx| self.formula_x = self.gpa.dupe(u8, fx) catch &.{};
    if (jsonStr(obj, "formulaY")) |fy| self.formula_y = self.gpa.dupe(u8, fy) catch &.{};
}

/// The project-level page format + formulas as a PageMeta view (borrows the owned slices).
pub fn pageMeta(self: *Session) server.PageMeta {
    return .{
        .page_size = self.page_size,
        .custom_w = self.custom_page_w,
        .custom_h = self.custom_page_h,
        .allow_formulas = self.allow_formulas,
        .formula_x = self.formula_x,
        .formula_y = self.formula_y,
    };
}

/// Build the browser-compatible layout JSON for the current state (caller owns it).
pub fn currentLayoutJson(self: *Session) ![]u8 {
    const st = self.state();
    const img = self.current();
    // No explicit crop means the WHOLE rotated original — stated rather than omitted: the GUIs auto-crop
    // to the page aspect unless the layout names a cropRect, stranding lines drawn outside the page.
    const crop: ?server.CropRect = if (st.crop) |c|
        .{ .x = c.x, .y = c.y, .w = c.w, .h = c.h }
    else
        .{ .x = 0, .y = 0, .w = @intCast(img.width), .h = @intCast(img.height) };
    return server.buildLayout(self.gpa, @intCast(img.width), @intCast(img.height), st.lines(), st.filter_mode, st.filter_color, crop, st.rotation, self.pageMeta());
}

/// Page-format label beside the px size, e.g. "A4 21×29.7cm" (or "custom <w>×<h>cm"). Shares the one
/// derivation with the one-shot pipeline's wrote line (page.pageLabelAlloc); caller owns it.
pub fn pageFormatLabel(self: *Session) ![]u8 {
    const img = self.current();
    return pipeline.pageLabelAlloc(self.gpa, self.page_size, self.custom_page_w, self.custom_page_h, img.width, img.height);
}

/// Set the picked page format (canonical "A0".."C10" or "custom"), owned copy. It drives
/// the header label, the layout `pageSize` written on save/sync, and the /blank default.
pub fn setPageSize(self: *Session, name: []const u8) !void {
    const dup = try self.gpa.dupe(u8, name);
    if (self.page_size.len != 0) self.gpa.free(self.page_size);
    self.page_size = dup;
}

/// Replace the displayed label (e.g. after a rename), owned copy.
pub fn setLabel(self: *Session, name: []const u8) !void {
    const dup = try self.gpa.dupe(u8, name);
    if (self.label) |l| self.gpa.free(l);
    self.label = dup;
}

/// What a formula's names resolve to in this session: the page in cm and, once an image is
/// open, its pixels. The console has no display-unit switch, so PAGE_WIDTH reads cm.
pub fn formulaContext(self: *Session) core.FormulaCtx {
    const img = self.working;
    const w: usize = if (img) |i| i.width else 0;
    const h: usize = if (img) |i| i.height else 0;
    const page = page_mod.pageCmFor(self.page_size, self.custom_page_w, self.custom_page_h, w, h);
    var ctx = core.FormulaCtx{ .page_w_cm = page.w, .page_h_cm = page.h };
    if (img != null) {
        ctx.image_w = @floatFromInt(w);
        ctx.image_h = @floatFromInt(h);
    }
    return ctx;
}

/// Set the x or y transform formula (validated via the shared parser with this session's named
/// values in reach). Returns false on an invalid expression, state unchanged.
pub fn setFormula(self: *Session, axis: u8, expr: []const u8) !bool {
    // The context is built BEFORE the expression is copied: it resolves a page name through
    // core.zstr's one scratch buffer, which would otherwise overwrite the copy of `expr`.
    const ctx = formulaContext(self);
    if (expr.len != 0 and !core.validateFormulaCtx(core.zstr(expr) orelse return false, ctx)) return false;
    const dup = try self.gpa.dupe(u8, expr);
    const slot = if (axis == 'y') &self.formula_y else &self.formula_x;
    if (slot.len != 0) self.gpa.free(slot.*);
    slot.* = dup;
    if (expr.len != 0) self.allow_formulas = true;
    return true;
}

/// Toggle whether formulas apply on the saved layout (keeps the expressions).
pub fn setAllowFormulas(self: *Session, on: bool) void {
    self.allow_formulas = on;
}

/// Clear both formula expressions and disable formulas (keeps the page format).
pub fn clearFormulas(self: *Session) void {
    if (self.formula_x.len != 0) self.gpa.free(self.formula_x);
    if (self.formula_y.len != 0) self.gpa.free(self.formula_y);
    self.formula_x = &.{};
    self.formula_y = &.{};
    self.allow_formulas = false;
}

/// Reset the page format + formulas to "unset" (frees owned strings).
pub fn clearFormat(self: *Session) void {
    self.clearFormulas();
    if (self.page_size.len != 0) self.gpa.free(self.page_size);
    self.page_size = &.{};
    self.custom_page_w = 0;
    self.custom_page_h = 0;
}

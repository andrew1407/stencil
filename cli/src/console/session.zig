//! Console working-image state. Structured (browser-compatible) model: an untouched
//! ORIGINAL image plus a stack of `EditState` snapshots (rotation + crop + filter + lines).
//! The current view is DERIVED on demand — rotate → crop → filter → rasterize lines — so the
//! exact same state can be serialized as a layout the browser/desktop editors render, making
//! every CLI edit (crop/rotate/filter/draw) sync to peers, not just baked into a `result`.
//! `/undo`, `/redo`, `/reset` move a cursor over the snapshots and rebuild the view.
const std = @import("std");
const image = @import("../image.zig");
const server = @import("../serverClient.zig");
const core = @import("../core.zig");
const pipeline = @import("../pipeline.zig");
const layout_mod = @import("../layout.zig");

const max_states = 64; // pristine + up to 63 undoable edits; older edits drop off the front

/// One editing snapshot, mirroring the browser layout: a rotation (0..3 clockwise quarters,
/// applied to the original FIRST), a crop rect in rotated-original pixels, an image filter
/// (mode "none"|"bw"|"sepia"|"custom" + custom hex color), and the drawn lines as a JSON array
/// string (browser line schema). All owned. Empty `lines_json` means "[]".
pub const EditState = struct {
    rotation: i32 = 0,
    crop: ?core.Rect = null,
    filter_mode: []u8 = &.{},
    filter_color: []u8 = &.{},
    lines_json: []u8 = &.{},

    fn deinit(self: *EditState, gpa: std.mem.Allocator) void {
        if (self.filter_mode.len != 0) gpa.free(self.filter_mode);
        if (self.filter_color.len != 0) gpa.free(self.filter_color);
        if (self.lines_json.len != 0) gpa.free(self.lines_json);
        self.* = .{};
    }

    fn dupe(self: EditState, gpa: std.mem.Allocator) !EditState {
        var out = EditState{ .rotation = self.rotation, .crop = self.crop };
        errdefer out.deinit(gpa);
        out.filter_mode = try gpa.dupe(u8, self.filter_mode);
        out.filter_color = try gpa.dupe(u8, self.filter_color);
        out.lines_json = try gpa.dupe(u8, self.lines_json);
        return out;
    }

    fn lines(self: EditState) []const u8 {
        return if (self.lines_json.len == 0) "[]" else self.lines_json;
    }
};

pub const Session = struct {
    gpa: std.mem.Allocator,
    label: ?[]u8 = null, // owned display label (the source path / URL / "blank" / "clipboard")
    temp: bool = false, // in-memory only (URL, blank, clipboard), not backed by a file on disk
    default_fmt: image.Format = .png,
    original: ?image.Rgba8 = null, // the untouched base image (owned); every view derives from it
    history: std.ArrayList(EditState) = .empty, // [0] = pristine; the current state is history[cursor]
    cursor: usize = 0,
    working: ?image.Rgba8 = null, // the derived current view (owned), rebuilt on every change

    // ── Server connections (collaboration) ──
    servers: std.ArrayList(server.Client) = .empty, // connected servers (REST clients)
    sync: bool = false, // when on, edits auto-upload the layout + result to the active remote
    dirty: bool = false, // a pending sync upload coalesced from a burst of edits (see handlers.flushSync)
    remote_url: ?[]u8 = null, // owned base URL of the active fetched project's server
    remote_id: ?[]u8 = null, // owned id of the active fetched project
    remote_version: i64 = 0, // last server version we hold for the active project (LWW guard for auto-pull)
    remote_color: ?[]u8 = null, // owned active project's custom name colour ("#rrggbb"); null/"" = default
    events: ?server.EditConn = null, // live read-only project-events feed (opened while syncing)
    events_url: ?[]u8 = null, // owned base URL the events feed is connected to

    // Page format + x/y formulas, set via /format or round-tripped through a fetched layout.
    page_size: []u8 = &.{}, // "" | a named format ("A0".."C10") | "custom"
    custom_page_w: f64 = 0, // cm; 0 = unset
    custom_page_h: f64 = 0,
    allow_formulas: bool = false,
    formula_x: []u8 = &.{}, // "" = identity transform
    formula_y: []u8 = &.{},

    /// True when a fetched server project is active (a target for sync / manual push).
    pub fn hasRemote(self: *const Session) bool {
        return self.remote_id != null and self.remote_url != null;
    }

    pub fn deinit(self: *Session) void {
        self.clearAll();
        self.closeEvents();
        self.history.deinit(self.gpa);
        for (self.servers.items) |*c| c.deinit();
        self.servers.deinit(self.gpa);
        self.clearRemote();
    }

    // ── live events feed ──
    /// Open (or replace) the read-only project-events subscription to `client`'s server.
    /// Best-effort: a failed connect leaves the feed closed and is not fatal.
    pub fn openEvents(self: *Session, client: *server.Client) void {
        self.closeEvents();
        const conn = server.EditConn.open(self.gpa, client.io, client.base, client.token, "stencil-cli") catch return;
        const url = self.gpa.dupe(u8, client.base) catch {
            var c = conn;
            c.deinit();
            return;
        };
        self.events = conn;
        self.events_url = url;
    }

    pub fn closeEvents(self: *Session) void {
        if (self.events) |*e| e.deinit();
        self.events = null;
        if (self.events_url) |u| self.gpa.free(u);
        self.events_url = null;
    }

    // ── connection helpers ──
    pub fn findServer(self: *Session, url: []const u8) ?*server.Client {
        for (self.servers.items) |*c| {
            if (std.mem.eql(u8, c.base, url)) return c;
        }
        return null;
    }

    /// Index of a connected server by base URL, for in-place replacement (`/reconnect`).
    pub fn indexOfServer(self: *Session, url: []const u8) ?usize {
        for (self.servers.items, 0..) |*c, i| {
            if (std.mem.eql(u8, c.base, url)) return i;
        }
        return null;
    }

    pub fn dropServer(self: *Session, url: []const u8) bool {
        for (self.servers.items, 0..) |*c, i| {
            if (std.mem.eql(u8, c.base, url)) {
                if (self.events_url != null and std.mem.eql(u8, self.events_url.?, url)) self.closeEvents();
                c.deinit();
                _ = self.servers.orderedRemove(i);
                return true;
            }
        }
        return false;
    }

    /// Record the active remote project (owns copies of url + id).
    pub fn setRemote(self: *Session, url: []const u8, id: []const u8) !void {
        const u = try self.gpa.dupe(u8, url);
        errdefer self.gpa.free(u);
        const i = try self.gpa.dupe(u8, id);
        self.clearRemote();
        self.remote_url = u;
        self.remote_id = i;
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

    /// Set the active project's custom name colour ("#rrggbb" or "" to clear), owned copy.
    pub fn setRemoteColor(self: *Session, color: []const u8) !void {
        const c = try self.gpa.dupe(u8, color);
        if (self.remote_color) |old| self.gpa.free(old);
        self.remote_color = c;
    }

    pub fn clearRemote(self: *Session) void {
        if (self.remote_url) |u| self.gpa.free(u);
        if (self.remote_id) |i| self.gpa.free(i);
        if (self.remote_color) |c| self.gpa.free(c);
        self.remote_url = null;
        self.remote_id = null;
        self.remote_color = null;
        self.remote_version = 0;
    }

    // ── image lifecycle ──
    pub fn hasImage(self: *Session) bool {
        return self.original != null;
    }

    /// The current derived view (valid whenever an image is loaded).
    pub fn current(self: *Session) *image.Rgba8 {
        return &self.working.?;
    }

    /// Number of history states (for the "[n/m]" position indicator).
    pub fn stateCount(self: *Session) usize {
        return self.history.items.len;
    }

    /// The current editing snapshot.
    pub fn state(self: *Session) EditState {
        return self.history.items[self.cursor];
    }

    /// Replace the whole session with a freshly loaded source: a pristine (un-rotated,
    /// un-cropped, un-filtered) state over `img` as the new original.
    pub fn loadImage(self: *Session, img: image.Rgba8, label: []const u8, temp: bool, fmt: image.Format) !void {
        const dup = self.gpa.dupe(u8, label) catch |e| return freeImg(self.gpa, img, e);
        self.clearAll();
        self.history.append(self.gpa, .{}) catch |e| {
            self.gpa.free(dup);
            return freeImg(self.gpa, img, e);
        };
        self.original = img;
        self.label = dup;
        self.temp = temp;
        self.default_fmt = fmt;
        self.cursor = 0;
        try self.rebuild();
    }

    /// Rebuild the derived view from the original + the current snapshot:
    /// rotate → crop → filter → rasterize lines. Replaces `working`.
    fn rebuild(self: *Session) !void {
        const orig = self.original orelse return;
        var img = image.Rgba8{ .width = orig.width, .height = orig.height, .pixels = try self.gpa.dupe(u8, orig.pixels) };
        errdefer img.deinit(self.gpa);
        const st = self.history.items[self.cursor];
        if (@mod(st.rotation, 4) != 0) try pipeline.applyRotateBy(self.gpa, &img, st.rotation);
        if (st.crop) |cr| try pipeline.cropToRect(self.gpa, &img, cr);
        if (st.filter_mode.len != 0 and !std.ascii.eqlIgnoreCase(st.filter_mode, "none")) {
            const arg = if (std.ascii.eqlIgnoreCase(st.filter_mode, "custom")) st.filter_color else st.filter_mode;
            pipeline.applyFilterMode(self.gpa, &img, arg);
        }
        rasterizeLinesJson(self.gpa, &img, st.lines());
        if (self.working) |*w| w.deinit(self.gpa);
        self.working = img;
    }

    /// Push `next` as the new current state (dropping any redo states), then rebuild the view.
    /// Takes ownership of `next` only on a successful append; on append failure the caller's
    /// errdefer frees it. A rebuild failure is non-fatal (the old view simply remains).
    fn pushState(self: *Session, next: EditState) !void {
        self.dropAfterCursor();
        try self.history.append(self.gpa, next); // append fails BEFORE ownership → caller frees
        self.cursor = self.history.items.len - 1;
        while (self.history.items.len > max_states) {
            self.history.items[1].deinit(self.gpa);
            _ = self.history.orderedRemove(1);
            self.cursor -= 1;
        }
        self.rebuild() catch {};
    }

    // ── editing ops (each pushes a snapshot + rebuilds) ──

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
    fn adoptLayoutMeta(self: *Session, layout_bytes: []const u8) void {
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
    fn pageMeta(self: *Session) server.PageMeta {
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
        const crop: ?server.CropRect = if (st.crop) |c|
            .{ .x = c.x, .y = c.y, .w = c.w, .h = c.h }
        else
            null;
        return server.buildLayout(self.gpa, @intCast(img.width), @intCast(img.height), st.lines(), st.filter_mode, st.filter_color, crop, st.rotation, self.pageMeta());
    }

    /// Page-format label shown next to the px size, e.g. "A4 21×29.7cm" (picked size oriented
    /// to the image, or "custom <w>×<h>cm"). Shares the one derivation with the one-shot
    /// pipeline's wrote line (pipeline.pageLabelAlloc). Owned by the caller.
    pub fn pageFormatLabel(self: *Session) ![]u8 {
        const img = self.current();
        return pipeline.pageLabelAlloc(self.gpa, self.page_size, self.custom_page_w, self.custom_page_h, img.width, img.height);
    }

    pub fn undo(self: *Session) bool {
        if (self.cursor == 0) return false;
        self.cursor -= 1;
        self.rebuild() catch {};
        return true;
    }

    pub fn redo(self: *Session) bool {
        if (self.cursor + 1 >= self.history.items.len) return false;
        self.cursor += 1;
        self.rebuild() catch {};
        return true;
    }

    /// Revert to the pristine state, dropping every edit and the redo history.
    pub fn revert(self: *Session) void {
        self.cursor = 0;
        self.dropAfterCursor();
        self.rebuild() catch {};
    }

    fn dropAfterCursor(self: *Session) void {
        var i = self.history.items.len;
        while (i > self.cursor + 1) : (i -= 1) self.history.items[i - 1].deinit(self.gpa);
        self.history.shrinkRetainingCapacity(self.cursor + 1);
    }

    pub fn clearAll(self: *Session) void {
        for (self.history.items) |*st| st.deinit(self.gpa);
        self.history.clearRetainingCapacity();
        if (self.original) |*o| o.deinit(self.gpa);
        self.original = null;
        if (self.working) |*w| w.deinit(self.gpa);
        self.working = null;
        self.cursor = 0;
        if (self.label) |l| self.gpa.free(l);
        self.label = null;
        self.temp = false;
        self.default_fmt = .png;
        self.clearFormat();
    }

    /// Set the x or y transform formula (validated via the shared parser; a non-empty
    /// expression enables formulas). Returns false on an invalid expression, state unchanged.
    pub fn setFormula(self: *Session, axis: u8, expr: []const u8) !bool {
        if (expr.len != 0 and !core.validateFormula(self.gpa, expr, axis)) return false;
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
};

fn freeImg(gpa: std.mem.Allocator, img: image.Rgba8, e: anyerror) anyerror {
    var m = img;
    m.deinit(gpa);
    return e;
}

/// Clamp a rect to lie within a `w`×`h` image (width/height ≥ 1).
fn clampRect(r: core.Rect, w: i32, h: i32) core.Rect {
    var out = r;
    out.w = std.math.clamp(r.w, 1, w);
    out.h = std.math.clamp(r.h, 1, h);
    out.x = std.math.clamp(r.x, 0, w - out.w);
    out.y = std.math.clamp(r.y, 0, h - out.h);
    return out;
}

/// Map a rect through `n` clockwise quarter-turns of its `w`×`h` containing image, returning
/// the rect in the rotated image's pixel space. Pure (axis-aligned 90° steps). Unit-tested.
fn rotateRectQuarters(rect: core.Rect, w: i32, h: i32, n: i32) core.Rect {
    var r = rect;
    var cw = w;
    var ch = h;
    var q = core.normalizeQuarters(n);
    while (q > 0) : (q -= 1) {
        // One clockwise step: new dims (ch, cw); (x,y) → (ch - y - rh, x). Compute into a
        // temp first — assigning a struct literal that reads `r` would alias the in-place write.
        const nr = core.Rect{ .x = ch - r.y - r.h, .y = r.x, .w = r.h, .h = r.w };
        r = nr;
        const t = cw;
        cw = ch;
        ch = t;
    }
    return r;
}

/// Rasterize the lines in a JSON array string onto `img` (best-effort; bad JSON draws nothing).
fn rasterizeLinesJson(gpa: std.mem.Allocator, img: *image.Rgba8, lines_json: []const u8) void {
    const wrapped = std.fmt.allocPrint(gpa, "{{\"lines\":{s}}}", .{lines_json}) catch return;
    defer gpa.free(wrapped);
    var parsed = layout_mod.parse(gpa, wrapped) catch return;
    defer parsed.deinit();
    for (parsed.lines) |line| {
        core.rasterizeLine(img.pixels, @intCast(img.width), @intCast(img.height), line);
    }
}

/// Extract the `lines` array of a layout JSON document as an owned JSON array string ("[]" if
/// absent). Caller owns the result.
fn extractLinesJson(gpa: std.mem.Allocator, layout_bytes: []const u8) ![]u8 {
    var parsed = std.json.parseFromSlice(std.json.Value, gpa, layout_bytes, .{}) catch return gpa.dupe(u8, "[]");
    defer parsed.deinit();
    if (parsed.value == .object) {
        if (parsed.value.object.get("lines")) |lv| {
            if (lv == .array) return std.json.Stringify.valueAlloc(gpa, lv, .{});
        }
    }
    return gpa.dupe(u8, "[]");
}

/// Concatenate two JSON array strings ("[...]") into one. Pure string work. Caller owns it.
fn mergeLinesJson(gpa: std.mem.Allocator, a: []const u8, b: []const u8) ![]u8 {
    const ai = innerArray(a);
    const bi = innerArray(b);
    if (ai.len == 0) return gpa.dupe(u8, if (bi.len == 0) "[]" else b);
    if (bi.len == 0) return gpa.dupe(u8, a);
    return std.fmt.allocPrint(gpa, "[{s},{s}]", .{ ai, bi });
}

/// The contents between the outermost `[` `]` of a JSON array string, trimmed (empty if none).
fn innerArray(s: []const u8) []const u8 {
    const t = std.mem.trim(u8, s, " \t\r\n");
    if (t.len < 2 or t[0] != '[' or t[t.len - 1] != ']') return "";
    return std.mem.trim(u8, t[1 .. t.len - 1], " \t\r\n");
}

/// Read a server layout document into an EditState (rotation, crop, filter, lines).
fn parseLayoutInto(gpa: std.mem.Allocator, layout_bytes: []const u8, out: *EditState) !void {
    out.lines_json = try extractLinesJson(gpa, layout_bytes);
    var parsed = std.json.parseFromSlice(std.json.Value, gpa, layout_bytes, .{}) catch return;
    defer parsed.deinit();
    if (parsed.value != .object) return;
    const obj = parsed.value.object;
    if (jsonStr(obj, "imageFilter")) |m| out.filter_mode = try gpa.dupe(u8, m);
    if (jsonStr(obj, "filterColor")) |c| out.filter_color = try gpa.dupe(u8, c);
    if (jsonInt(obj, "rotationQuarters")) |r| out.rotation = core.normalizeQuarters(@intCast(r));
    if (obj.get("cropRect")) |cv| {
        if (cv == .object) {
            const co = cv.object;
            out.crop = .{
                .x = @intFromFloat(jsonNum(co, "x")),
                .y = @intFromFloat(jsonNum(co, "y")),
                .w = @intFromFloat(jsonNum(co, "width")),
                .h = @intFromFloat(jsonNum(co, "height")),
            };
        }
    }
}

fn jsonStr(obj: std.json.ObjectMap, key: []const u8) ?[]const u8 {
    if (obj.get(key)) |v| {
        if (v == .string) return v.string;
    }
    return null;
}

fn jsonInt(obj: std.json.ObjectMap, key: []const u8) ?i64 {
    if (obj.get(key)) |v| {
        return switch (v) {
            .integer => |i| i,
            .float => |f| @intFromFloat(f),
            else => null,
        };
    }
    return null;
}

fn jsonNum(obj: std.json.ObjectMap, key: []const u8) f64 {
    if (obj.get(key)) |v| {
        return switch (v) {
            .integer => |i| @floatFromInt(i),
            .float => |f| f,
            else => 0,
        };
    }
    return 0;
}

const testing = std.testing;

test "rotateRectQuarters maps a rect through clockwise quarter-turns" {
    // A 10x4 rect at (1,2) in a 100x50 image, rotated once clockwise → 50x100 image.
    const r1 = rotateRectQuarters(.{ .x = 1, .y = 2, .w = 10, .h = 4 }, 100, 50, 1);
    // new x = ch - y - rh = 50 - 2 - 4 = 44; new y = x = 1; w=rh=4; h=rw=10.
    try testing.expectEqual(@as(i32, 44), r1.x);
    try testing.expectEqual(@as(i32, 1), r1.y);
    try testing.expectEqual(@as(i32, 4), r1.w);
    try testing.expectEqual(@as(i32, 10), r1.h);
    // Four turns returns to the original.
    const r4 = rotateRectQuarters(.{ .x = 1, .y = 2, .w = 10, .h = 4 }, 100, 50, 4);
    try testing.expectEqual(@as(i32, 1), r4.x);
    try testing.expectEqual(@as(i32, 2), r4.y);
    try testing.expectEqual(@as(i32, 10), r4.w);
    try testing.expectEqual(@as(i32, 4), r4.h);
}

test "mergeLinesJson concatenates arrays, handles empties" {
    const a = testing.allocator;
    const m1 = try mergeLinesJson(a, "[{\"a\":1}]", "[{\"b\":2}]");
    defer a.free(m1);
    try testing.expectEqualStrings("[{\"a\":1},{\"b\":2}]", m1);
    const m2 = try mergeLinesJson(a, "[]", "[{\"b\":2}]");
    defer a.free(m2);
    try testing.expectEqualStrings("[{\"b\":2}]", m2);
    const m3 = try mergeLinesJson(a, "[{\"a\":1}]", "[]");
    defer a.free(m3);
    try testing.expectEqualStrings("[{\"a\":1}]", m3);
}

test "extractLinesJson pulls the lines array, defaults to []" {
    const a = testing.allocator;
    const l = try extractLinesJson(a, "{\"lines\":[{\"color\":\"#f00\"}],\"imageFilter\":\"bw\"}");
    defer a.free(l);
    try testing.expectEqualStrings("[{\"color\":\"#f00\"}]", l);
    const none = try extractLinesJson(a, "{\"imageFilter\":\"bw\"}");
    defer a.free(none);
    try testing.expectEqualStrings("[]", none);
}

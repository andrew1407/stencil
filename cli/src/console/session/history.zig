//! The working image and its undo/redo stack: loading a picture, pushing one edit state,
//! stepping back and forth, and dropping everything.
const Session = @import("../session.zig").Session;
const image = @import("../../image.zig");
const EditState = @import("../session.zig").EditState;
const max_states = @import("../session.zig").max_states;
const freeImg = @import("../session.zig").freeImg;
const rasterizeLinesJson = @import("../session.zig").rasterizeLinesJson;


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

/// Push `next` as the new current state (dropping any redo states), then rebuild the view.
/// Takes ownership of `next` only on a successful append; on append failure the caller's
/// errdefer frees it. A rebuild failure is non-fatal (the old view simply remains).
pub fn pushState(self: *Session, next: EditState) !void {
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

pub fn dropAfterCursor(self: *Session) void {
    var i = self.history.items.len;
    while (i > self.cursor + 1) : (i -= 1) self.history.items[i - 1].deinit(self.gpa);
    self.history.shrinkRetainingCapacity(self.cursor + 1);
}

pub fn clearAll(self: *Session) void {
    for (self.history.items) |*st| st.deinit(self.gpa);
    self.history.clearRetainingCapacity();
    if (self.original) |*o| o.deinit(self.gpa);
    self.original = null;
    if (self.source_bytes) |b| self.gpa.free(b);
    self.source_bytes = null;
    if (self.working) |*w| w.deinit(self.gpa);
    self.working = null;
    self.base.deinit(self.gpa);
    if (self.prompt_b64) |b| self.gpa.free(b);
    self.prompt_b64 = null;
    self.prompt_digest = 0;
    self.cursor = 0;
    if (self.label) |l| self.gpa.free(l);
    self.label = null;
    self.temp = false;
    self.default_fmt = .png;
    self.clearFormat();
}

/// Replace the whole session with a freshly loaded source: a pristine (un-rotated,
/// un-cropped, un-filtered) state over `img` as the new original.
/// `source_bytes` (optional, ownership transferred) are the raw encoded bytes of `img` in
/// `fmt`; kept so a .stencil bundle embeds the untouched original. Pass null when none exist
/// (blank / clipboard / a decoded peer image) and the bundle re-encodes from pixels.
pub fn loadImage(self: *Session, img: image.Rgba8, label: []const u8, temp: bool, fmt: image.Format, source_bytes: ?[]u8) !void {
    const dup = self.gpa.dupe(u8, label) catch |e| {
        if (source_bytes) |b| self.gpa.free(b);
        return freeImg(self.gpa, img, e);
    };
    self.clearAll();
    self.history.append(self.gpa, .{}) catch |e| {
        self.gpa.free(dup);
        if (source_bytes) |b| self.gpa.free(b);
        return freeImg(self.gpa, img, e);
    };
    self.original = img;
    self.source_bytes = source_bytes;
    self.label = dup;
    self.temp = temp;
    self.default_fmt = fmt;
    self.cursor = 0;
    try self.rebuild();
}

/// Rebuild the derived view from the original + the current snapshot:
/// rotate → crop → filter → rasterize lines. Replaces `working`.
pub fn rebuild(self: *Session) !void {
    if (self.original == null) return;
    var img = try self.viewWithoutLines();
    errdefer img.deinit(self.gpa);
    rasterizeLinesJson(self.gpa, &img, self.history.items[self.cursor].lines());
    if (self.working) |*w| w.deinit(self.gpa);
    self.working = img;
}

/// The current view derived WITHOUT the drawn lines (rotate → crop → filter only) —
/// the base `rebuild` rasterizes onto, and the base a variant render starts from so its
/// own filter never recolours the lines. Caller owns the result; needs a loaded image.
/// Served from `base`, so redrawing lines never re-runs the transforms (derivedView.zig).
pub fn viewWithoutLines(self: *Session) !image.Rgba8 {
    const st = self.history.items[self.cursor];
    return self.base.view(self.gpa, self.original.?, .{
        .rotation = st.rotation,
        .crop = st.crop,
        .mode = st.filter_mode,
        .color = st.filter_color,
    });
}

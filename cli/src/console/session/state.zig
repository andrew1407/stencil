//! The session's state records: one undoable `EditState` snapshot (rotation + crop +
//! filter + lines — the browser-compatible model) and one `Attachment` the turn will send.
const std = @import("std");
const image = @import("../../image.zig");
const core = @import("../../core.zig");

pub const max_states = 64; // pristine + up to 63 undoable edits; older edits drop off the front

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

    pub fn deinit(self: *EditState, gpa: std.mem.Allocator) void {
        if (self.filter_mode.len != 0) gpa.free(self.filter_mode);
        if (self.filter_color.len != 0) gpa.free(self.filter_color);
        if (self.lines_json.len != 0) gpa.free(self.lines_json);
        self.* = .{};
    }

    pub fn dupe(self: EditState, gpa: std.mem.Allocator) !EditState {
        var out = EditState{ .rotation = self.rotation, .crop = self.crop };
        errdefer out.deinit(gpa);
        out.filter_mode = try gpa.dupe(u8, self.filter_mode);
        out.filter_color = try gpa.dupe(u8, self.filter_color);
        out.lines_json = try gpa.dupe(u8, self.lines_json);
        return out;
    }

    pub fn lines(self: EditState) []const u8 {
        return if (self.lines_json.len == 0) "[]" else self.lines_json;
    }
};

/// One image the user brought into the turn with `/upload` (contract §2.1/§7): its
/// label (the path/URL it came from), the raw ENCODED bytes — kept instead of pixels so
/// a whole turn of attachments costs kilobytes, and so a `save` embeds the untouched
/// original — plus how to re-encode it. All owned by the session.
pub const Attachment = struct {
    label: []u8,
    bytes: []u8,
    fmt: image.Format = .png,
    temp: bool = false, // came from a URL/in-memory source, not a file on disk

    pub fn deinit(self: *Attachment, gpa: std.mem.Allocator) void {
        gpa.free(self.label);
        gpa.free(self.bytes);
        self.* = undefined;
    }
};

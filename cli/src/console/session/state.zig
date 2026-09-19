//! The session's state records: one undoable `EditState` snapshot (rotation + crop +
//! filter + lines — the browser-compatible model) and one `Attachment` the turn will send.
const std = @import("std");
const image = @import("../../image.zig");
const core = @import("../../core.zig");

pub const max_states = 64; // pristine + up to 63 undoable edits; older edits drop off the front

/// One editing snapshot mirroring the browser layout: rotation (0..3 clockwise quarters, applied to
/// the original first), a crop rect in rotated-original pixels, a filter, and the lines as JSON.
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

/// One image brought into the turn with `/upload` (§2.1/§7): its label plus the raw ENCODED bytes —
/// kept instead of pixels, so a turn costs kilobytes and a `save` embeds the untouched original.
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

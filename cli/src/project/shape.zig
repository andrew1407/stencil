//! The `.stencil` format's shape: its sentinel, version and the two records the codec
//! reads and writes.
const std = @import("std");
const image = @import("../media/image.zig");

pub const FORMAT = "stencil-project";
pub const VERSION = 1;

pub const Error = error{ NotStencilProject, UnsupportedVersion, NoImage, BadImageData };

/// A parsed `.stencil` document. All slices are owned by `arena`; free with deinit().
pub const Project = struct {
    arena: std.heap.ArenaAllocator,
    name: []const u8 = "Untitled",
    color: []const u8 = "",
    description: []const u8 = "",
    source: []const u8 = "",
    resource: []const u8 = "",
    blank: bool = false,
    blank_color: []const u8 = "",
    image_bytes: []u8 = &.{}, // decoded ENCODED image bytes (still PNG/JPEG/…), ready for image.decode
    image_ext: []const u8 = "png",
    image_w: i64 = 0,
    image_h: i64 = 0,
    layout_json: []const u8 = "{}", // the `layout` sub-object, re-stringified
    chat_json: ?[]const u8 = null, // the optional top-level `chat` block (llm-contract §12.1), re-stringified

    pub fn deinit(self: *Project) void {
        self.arena.deinit();
    }
};

/// Fields to bundle a project: `image_bytes` are ENCODED (image.encode'd); empty metadata is omitted.
pub const BuildOpts = struct {
    name: []const u8,
    color: []const u8 = "",
    description: []const u8 = "",
    source: []const u8 = "",
    resource: []const u8 = "",
    blank: bool = false,
    blank_color: []const u8 = "",
    image_bytes: []const u8,
    image_ext: []const u8,
    image_w: usize,
    image_h: usize,
    layout_json: []const u8,
    chat_json: []const u8 = "", // §12.1 persisted-chat document (already valid JSON); "" = omit the key
};

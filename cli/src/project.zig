//! `.stencil` project files: parse/build the portable single-file JSON format bundling the
//! ORIGINAL image + export layout + metadata (Zig-side codec/JSON, like layout.zig). Also the
//! single home for the session⇄file bridge (loadInto/saveInto) both the console handlers and the
//! one-shot pipeline share, so a project's load/save can't drift between the two entry points.
const std = @import("std");
const image = @import("image.zig");
const net = @import("net.zig");
const pipeline = @import("pipeline.zig");
const report = @import("report.zig");
const llm = @import("llm.zig");
const Session = @import("console/session.zig").Session;
const shape = @import("project/shape.zig");
const codec = @import("project/codec.zig");
const bridge = @import("project/bridge.zig");

pub const FORMAT = shape.FORMAT;
pub const VERSION = shape.VERSION;
pub const Error = shape.Error;
pub const Project = shape.Project;
pub const BuildOpts = shape.BuildOpts;

pub const isStencilPath = codec.isStencilPath;
pub const build = codec.build;
pub const parse = codec.parse;

pub const loadInto = bridge.loadInto;
pub const SaveMeta = bridge.SaveMeta;
pub const saveInto = bridge.saveInto;

const testing = std.testing;

// A real 1×1 red PNG data-URL (same fixture the browser test uses).
const RED_1x1 = "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mO4Y2T0HwAFbgJAIh+PxAAAAABJRU5ErkJggg==";

test "parse: accepts a minimal valid project and decodes its image" {
    const a = testing.allocator;
    const doc = "{\"format\":\"stencil-project\",\"version\":1,\"name\":\"red\"," ++
        "\"image\":{\"dataUrl\":\"" ++ RED_1x1 ++ "\",\"ext\":\"png\",\"w\":1,\"h\":1}," ++
        "\"layout\":{\"imageWidth\":1,\"imageHeight\":1,\"lines\":[],\"imageFilter\":\"bw\",\"rotationQuarters\":1}}";
    var p = try parse(a, doc);
    defer p.deinit();
    try testing.expectEqualStrings("red", p.name);
    try testing.expectEqualStrings("png", p.image_ext);
    try testing.expectEqual(@as(i64, 1), p.image_w);
    try testing.expect(p.image_bytes.len > 8); // a real PNG header + data
    try testing.expect(std.mem.indexOf(u8, p.layout_json, "\"imageFilter\":\"bw\"") != null);
}

test "parse: rejects a missing format sentinel and a too-new version" {
    const a = testing.allocator;
    try testing.expectError(Error.NotStencilProject, parse(a, "{\"version\":1}"));
    const newer = "{\"format\":\"stencil-project\",\"version\":999,\"image\":{\"dataUrl\":\"" ++ RED_1x1 ++ "\"}}";
    try testing.expectError(Error.UnsupportedVersion, parse(a, newer));
    try testing.expectError(Error.NoImage, parse(a, "{\"format\":\"stencil-project\",\"version\":1}"));
}

test "build → parse round-trips image + layout + metadata" {
    const a = testing.allocator;
    // Encode a tiny 2-byte "image" — build only base64s it; parse must return the same bytes.
    const img = [_]u8{ 0xDE, 0xAD, 0xBE, 0xEF };
    const doc = try build(a, .{
        .name = "shot",
        .color = "#7c3aed",
        .description = "a caption",
        .image_bytes = &img,
        .image_ext = "png",
        .image_w = 4,
        .image_h = 2,
        .layout_json = "{\"imageWidth\":4,\"imageHeight\":2,\"lines\":[],\"rotationQuarters\":0}",
    });
    defer a.free(doc);
    var p = try parse(a, doc);
    defer p.deinit();
    try testing.expectEqualStrings("shot", p.name);
    try testing.expectEqualStrings("#7c3aed", p.color);
    try testing.expectEqualStrings("a caption", p.description);
    try testing.expectEqualSlices(u8, &img, p.image_bytes);
    try testing.expectEqual(@as(i64, 4), p.image_w);
    try testing.expect(std.mem.indexOf(u8, p.layout_json, "\"imageWidth\":4") != null);
}

test "build → parse round-trips an optional chat block; absent by default" {
    const a = testing.allocator;
    const img = [_]u8{ 1, 2, 3 };

    // With a chat document (llm-contract §12.1) the top-level `chat` key round-trips verbatim.
    const chat = "{\"version\":1,\"savedAt\":7,\"messages\":[{\"role\":\"user\",\"text\":\"hi\"}]}";
    const with = try build(a, .{
        .name = "c",
        .image_bytes = &img,
        .image_ext = "png",
        .image_w = 1,
        .image_h = 1,
        .layout_json = "{}",
        .chat_json = chat,
    });
    defer a.free(with);
    var p = try parse(a, with);
    defer p.deinit();
    try testing.expect(p.chat_json != null);
    try testing.expect(std.mem.indexOf(u8, p.chat_json.?, "\"version\":1") != null);
    try testing.expect(std.mem.indexOf(u8, p.chat_json.?, "\"text\":\"hi\"") != null);

    // Without one (the default) the key is omitted from the document and parses back null.
    const without = try build(a, .{
        .name = "c",
        .image_bytes = &img,
        .image_ext = "png",
        .image_w = 1,
        .image_h = 1,
        .layout_json = "{}",
    });
    defer a.free(without);
    try testing.expect(std.mem.indexOf(u8, without, "\"chat\"") == null);
    var p2 = try parse(a, without);
    defer p2.deinit();
    try testing.expect(p2.chat_json == null);
}

test "isStencilPath matches only .stencil (any case)" {
    try testing.expect(isStencilPath("a/b.stencil"));
    try testing.expect(isStencilPath("X.STENCIL"));
    try testing.expect(!isStencilPath("a.stencil.png"));
    try testing.expect(!isStencilPath("a.json"));
}

test {
    _ = shape;
    _ = codec;
    _ = bridge;
}

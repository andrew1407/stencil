//! The pictures a `/prompt` turn sends (contract §7): the working image, the turn's
//! `/upload` attachments, and the contour edge map — each base64 PNG, each capped.
const std = @import("std");
const image = @import("../../media/image.zig");
const logo = @import("../../app/logo.zig");
const core = @import("../../core.zig");
const llm = @import("../../llm.zig");
const Session = @import("../session.zig").Session;
const Attachment = @import("../session.zig").Attachment;

pub fn promptImageB64(session: *Session) error{OutOfMemory}!?[]const u8 {
    const gpa = session.gpa;
    const cur = session.current().*;
    var hasher = std.hash.Wyhash.init(0);
    hasher.update(std.mem.asBytes(&cur.width)); // pixels alone can't tell 2×3 from 3×2
    hasher.update(cur.pixels);
    const digest = hasher.final();
    if (session.prompt_b64) |cached| {
        if (digest == session.prompt_digest) return cached;
    }
    const png = image.encode(gpa, cur, .png) catch |e| {
        logo.note("could not encode the image for attachment ({s}) — sending text only\n", .{@errorName(e)});
        return null;
    };
    defer gpa.free(png);
    if (png.len > llm.max_image_bytes) {
        logo.note("the working image encodes to {d} bytes — over the {d} MiB attachment cap, sending text only\n", .{ png.len, llm.max_image_bytes / (1024 * 1024) });
        return null;
    }
    const out = try gpa.alloc(u8, std.base64.standard.Encoder.calcSize(png.len));
    _ = std.base64.standard.Encoder.encode(out, png);
    if (session.prompt_b64) |old| gpa.free(old);
    session.prompt_b64 = out;
    session.prompt_digest = digest;
    return out;
}

/// One §2.1 turn attachment as a base64 PNG (the wire mapping sends `image/png` only, so a
/// JPEG/WebP upload is re-encoded). Null — never fatal — when it fails or lands over `cap`.
pub fn attachmentB64(gpa: std.mem.Allocator, at: Attachment, cap: usize) error{OutOfMemory}!?[]u8 {
    var img = image.decode(gpa, at.bytes) catch return null;
    defer img.deinit(gpa);
    const png = image.encode(gpa, img, .png) catch return null;
    defer gpa.free(png);
    if (png.len > cap) return null;
    const out = try gpa.alloc(u8, std.base64.standard.Encoder.calcSize(png.len));
    _ = std.base64.standard.Encoder.encode(out, png);
    return out;
}

/// The §7 edge map: the working image through the core contour filter, as base64 PNG. Null when
/// over `cap` or failing to encode. Current-turn only, never cached; caller owns the result.
pub fn edgeMapB64(session: *Session, cap: usize) error{OutOfMemory}!?[]u8 {
    return contourB64(session.gpa, session.current().*, cap);
}

/// Contour + PNG-encode + base64 a COPY of `src`; null over `cap` or on encode failure.
fn contourB64(gpa: std.mem.Allocator, src: image.Rgba8, cap: usize) error{OutOfMemory}!?[]u8 {
    var img = image.Rgba8{ .width = src.width, .height = src.height, .pixels = try gpa.dupe(u8, src.pixels) };
    defer img.deinit(gpa);
    core.applyContour(img.pixels, @intCast(img.width), @intCast(img.height));
    const png = image.encode(gpa, img, .png) catch return null;
    defer gpa.free(png);
    if (png.len > cap) return null;
    const out = try gpa.alloc(u8, std.base64.standard.Encoder.calcSize(png.len));
    _ = std.base64.standard.Encoder.encode(out, png);
    return out;
}

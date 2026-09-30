//! The console suites' shared rig: the PNG fixture, the working image, and a capture of what the
//! user is told through `logo.print`.
const std = @import("std");
const console = @import("../../src/console.zig");
const image = @import("../../src/media/image.zig");
const logo = @import("../../src/app/logo.zig");

pub const sample = @embedFile("sample.png");

pub fn cur(session: *console.Session) image.Rgba8 {
    return session.current().*;
}

/// Collects the console's human output (`logo.print`) so a test can assert on what the
/// user is actually told, using the same sink seam the full-screen console installs.
pub const Capture = struct {
    gpa: std.mem.Allocator,
    buf: std.ArrayList(u8) = .empty,

    pub fn init(gpa: std.mem.Allocator) Capture {
        return .{ .gpa = gpa };
    }

    pub fn deinit(self: *Capture) void {
        self.buf.deinit(self.gpa);
    }

    pub fn install(self: *Capture) void {
        logo.setSink(trampoline, self);
    }

    fn trampoline(ctx: *anyopaque, chunk: []const u8) void {
        const self: *Capture = @ptrCast(@alignCast(ctx));
        self.buf.appendSlice(self.gpa, chunk) catch {};
    }

    pub fn text(self: *const Capture) []const u8 {
        return self.buf.items;
    }
};

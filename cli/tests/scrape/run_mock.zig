//! The scrape run's test doubles: a `Deps` whose fetches, lines, dirs and writes are in-memory
//! maps, and a PNG header `sniff` can measure.
const std = @import("std");
const Deps = @import("../../src/scrape/deps.zig").Deps;
const png_sig = @import("../../src/scrape/sniff.zig").png_sig;

/// A 24-byte PNG header (signature + IHDR) carrying `w`×`h` (both < 256) so `sniff` measures it.
pub fn mkPng(a: std.mem.Allocator, w: u8, h: u8) []const u8 {
    const b = a.alloc(u8, 24) catch unreachable;
    @memcpy(b[0..8], &png_sig);
    @memcpy(b[8..12], &[_]u8{ 0, 0, 0, 0x0d }); // IHDR length 13
    @memcpy(b[12..16], "IHDR");
    @memcpy(b[16..20], &[_]u8{ 0, 0, 0, w }); // width, big-endian
    @memcpy(b[20..24], &[_]u8{ 0, 0, 0, h }); // height, big-endian
    return b;
}

pub const MockIo = struct {
    a: std.mem.Allocator,
    fetches: std.StringHashMap([]const u8),
    files: std.StringHashMap([]const u8),
    lines: std.ArrayListUnmanaged([]const u8) = .empty,
    dirs: std.ArrayListUnmanaged([]const u8) = .empty,

    pub fn init(a: std.mem.Allocator) MockIo {
        return .{
            .a = a,
            .fetches = std.StringHashMap([]const u8).init(a),
            .files = std.StringHashMap([]const u8).init(a),
        };
    }
    pub fn serve(self: *MockIo, url: []const u8, bytes: []const u8) !void {
        try self.fetches.put(url, bytes);
    }
    pub fn deps(self: *MockIo) Deps {
        return .{ .ctx = @ptrCast(self), .fetchFn = fetchFn, .emitFn = emitFn, .mkdirFn = mkdirFn, .writeFn = writeFn };
    }
    pub fn line(self: *MockIo, i: usize) []const u8 {
        return self.lines.items[i];
    }
    fn fetchFn(ptr: *anyopaque, a: std.mem.Allocator, _: std.Io, url: []const u8, _: bool) anyerror![]u8 {
        const self: *MockIo = @ptrCast(@alignCast(ptr));
        const v = self.fetches.get(url) orelse return error.HttpFailed;
        return a.dupe(u8, v);
    }
    fn emitFn(ptr: *anyopaque, s: []const u8) void {
        const self: *MockIo = @ptrCast(@alignCast(ptr));
        const dup = self.a.dupe(u8, s) catch return;
        self.lines.append(self.a, dup) catch {};
    }
    fn mkdirFn(ptr: *anyopaque, _: std.Io, path: []const u8) anyerror!void {
        const self: *MockIo = @ptrCast(@alignCast(ptr));
        try self.dirs.append(self.a, try self.a.dupe(u8, path));
    }
    fn writeFn(ptr: *anyopaque, _: std.Io, path: []const u8, data: []const u8) anyerror!void {
        const self: *MockIo = @ptrCast(@alignCast(ptr));
        try self.files.put(try self.a.dupe(u8, path), try self.a.dupe(u8, data));
    }
};

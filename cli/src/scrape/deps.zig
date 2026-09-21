//! The injected I/O seam. Everything the scrape loop touches outside the pure helpers —
//! fetching, the stderr lines, mkdir and write — goes through here, which is why the whole
//! loop is tested offline with no network and no disk.
const std = @import("std");
const net = @import("../net.zig");
const report = @import("../app/report.zig");


/// Injectable I/O seam so `runImpl`'s orchestration (fetch → filter → window → write → the §3 stderr
/// lines) is testable with no network and no disk. `run` wires the real net.fetch/report.print/cwd.
pub const Deps = struct {
    ctx: *anyopaque,
    fetchFn: *const fn (*anyopaque, std.mem.Allocator, std.Io, []const u8, bool) anyerror![]u8,
    emitFn: *const fn (*anyopaque, []const u8) void,
    mkdirFn: *const fn (*anyopaque, std.Io, []const u8) anyerror!void,
    writeFn: *const fn (*anyopaque, std.Io, []const u8, []const u8) anyerror!void,

    /// `strict` blocks loopback too: pass false for the user-named page URL, true for the
    /// media sub-resource URLs harvested from that (untrusted) page's content.
    pub fn fetch(self: Deps, a: std.mem.Allocator, io: std.Io, url: []const u8, strict: bool) ![]u8 {
        return self.fetchFn(self.ctx, a, io, url, strict);
    }
    /// Format one stderr line into `arena` and hand it to the sink (a no-op on OOM).
    pub fn emit(self: Deps, arena: std.mem.Allocator, comptime fmt: []const u8, a: anytype) void {
        const s = std.fmt.allocPrint(arena, fmt, a) catch return;
        self.emitFn(self.ctx, s);
    }
    /// The same as one `error: ` line (report.err's shape, over this sink).
    pub fn err(self: Deps, arena: std.mem.Allocator, comptime fmt: []const u8, a: anytype) void {
        const msg = std.fmt.allocPrint(arena, fmt, a) catch return;
        const line = std.fmt.allocPrint(arena, "{s}{s}", .{ report.errPrefix(), msg }) catch return;
        self.emitFn(self.ctx, line);
    }
    pub fn mkdir(self: Deps, io: std.Io, path: []const u8) !void {
        return self.mkdirFn(self.ctx, io, path);
    }
    pub fn write(self: Deps, io: std.Io, path: []const u8, data: []const u8) !void {
        return self.writeFn(self.ctx, io, path, data);
    }
};

pub fn realFetch(_: *anyopaque, a: std.mem.Allocator, io: std.Io, url: []const u8, strict: bool) anyerror![]u8 {
    return net.fetch(a, io, url, strict);
}
pub fn realEmit(_: *anyopaque, s: []const u8) void {
    report.print("{s}", .{s});
}
pub fn realMkdir(_: *anyopaque, io: std.Io, path: []const u8) anyerror!void {
    return std.Io.Dir.cwd().createDirPath(io, path);
}
pub fn realWrite(_: *anyopaque, io: std.Io, path: []const u8, data: []const u8) anyerror!void {
    return std.Io.Dir.cwd().writeFile(io, .{ .sub_path = path, .data = data });
}

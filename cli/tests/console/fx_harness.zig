//! The effect-pin rig: a fake terminal (the tty tap), a fake clock (an Io whose `now` is ours)
//! and a pacer that advances it, so a console effect records as frames — each frame's bytes in
//! order and the tick it landed on. Frames compare with tests/pins/fx/<name>.txt;
//! `STENCIL_UPDATE_FX_PINS=1 zig build test` records them.
const std = @import("std");
const tty = @import("../../src/console/screen/tty.zig");
const timing = @import("../../src/console/render/logoFx/timing.zig");
const screen = @import("../../src/console/screen.zig");
const logo = @import("../../src/app/logo.zig");
const theme = @import("../../src/app/theme.zig");
const skin = @import("../../src/app/skin.zig");
const ui = @import("../../src/console/ui.zig");
const hooks = @import("../../src/console/hooks.zig");
const Session = @import("../../src/console/session.zig").Session;
const console = @import("../../src/console.zig");
const loop = @import("../../src/console/loop.zig");
const text = @import("fx_text.zig");
const escape = text.escape;
pub const stripWrappers = text.stripWrappers;
const testing = std.testing;

pub const fake_fd: std.posix.fd_t = 997; // the tap takes every write to it; nothing reaches a tty
pub const start_ms: i64 = 1_000_000;
const real_offset_ms: i64 = 1_700_000_000_000; // the wall clock seeds the skins' pictures
const inline_max = 1536; // a frame this big is pinned by length + hash, not spelled out

extern "c" fn unsetenv(name: [*:0]const u8) c_int;

var fake_vt: std.Io.VTable = undefined;
var active: ?*Rig = null;

fn fakeNow(_: ?*anyopaque, clock: std.Io.Clock) std.Io.Timestamp {
    const r = active.?;
    const off: i96 = if (clock == .real) real_offset_ms * std.time.ns_per_ms else 0;
    return .{ .nanoseconds = r.now_ns + off };
}

pub const Rig = struct {
    gpa: std.mem.Allocator,
    threaded: std.Io.Threaded,
    io: std.Io = undefined,
    now_ns: i96 = start_ms * std.time.ns_per_ms,
    scr: screen.Screen = undefined,
    session: Session = undefined,
    idle_ctx: hooks.IdleCtx = undefined,
    frame: std.ArrayList(u8) = .empty, // bytes since the last wait
    out: std.ArrayList(u8) = .empty, // the golden being built
    writes: usize = 0,

    /// A full-screen console on the fake terminal, entered and seeded, recording from a clean slate.
    pub fn start(self: *Rig, gpa: std.mem.Allocator, rows: u16, cols: u16) !void {
        try self.init(gpa, rows, cols);
        try self.scr.enter();
        self.seed();
    }

    /// The fake terminal and clock with the console's default look; `scr.enter()` is the caller's.
    pub fn init(self: *Rig, gpa: std.mem.Allocator, rows: u16, cols: u16) !void {
        self.* = .{ .gpa = gpa, .threaded = std.Io.Threaded.init(gpa, .{}) };
        const base = self.threaded.io();
        fake_vt = base.vtable.*;
        fake_vt.now = fakeNow;
        self.io = .{ .userdata = base.userdata, .vtable = &fake_vt };
        active = self;
        _ = unsetenv("STENCIL_CONSOLE_MOUSE");
        _ = unsetenv("STENCIL_CONSOLE_REVEAL_SPEED");
        skin.resetForTest();
        logo.init(false, true);
        logo.setAccent(theme.rgbOf(theme.default_key));
        ui.setAccent(theme.default_key);
        ui.setInteractive(true);
        tty.tap = .{ .fd = fake_fd, .ctx = self, .write = onWrite };
        timing.pacer = .{ .ctx = self, .wait = onWait };
        self.session = Session{ .gpa = gpa };
        self.scr = screen.Screen{ .gpa = gpa, .io = self.io, .fd = fake_fd, .rows = rows, .cols = cols };
        self.idle_ctx = .{ .session = &self.session, .io = self.io, .screen = &self.scr };
    }

    /// Scrollback the effects have something to sweep over, painted instantly and not recorded.
    pub fn seed(self: *Rig) void {
        self.scr.setRevealSpeed(screen.speed_max);
        logo.print("{s}> {s}/theme\n", .{ logo.accentSeq(), logo.resetSeq() });
        logo.print("wrote out.png (640x480 px · A4 29.7×21cm)\n", .{});
        logo.note("the accent reaches this far\n", .{});
        logo.print("image: out.png (640x480 px · A4)\n", .{});
        self.scr.setRevealSpeed(screen.speed_default);
        self.reset();
    }

    pub fn deinit(self: *Rig) void {
        self.scr.deinit();
        self.session.deinit();
        self.frame.deinit(self.gpa);
        self.out.deinit(self.gpa);
        tty.tap = null;
        timing.pacer = null;
        active = null;
        skin.resetForTest();
        logo.setAccent(theme.rgbOf(theme.default_key));
        ui.setAccent(theme.default_key);
        ui.setInteractive(false);
        logo.init(false, false);
        self.threaded.deinit();
    }

    /// Forget what was recorded so far and restart the clock at `start_ms`.
    pub fn reset(self: *Rig) void {
        self.frame.clearRetainingCapacity();
        self.out.clearRetainingCapacity();
        self.writes = 0;
        self.now_ns = start_ms * std.time.ns_per_ms;
    }

    fn onWrite(ctx: *anyopaque, bytes: []const u8) void {
        const self: *Rig = @ptrCast(@alignCast(ctx));
        self.writes += 1;
        self.frame.appendSlice(self.gpa, bytes) catch {};
    }

    fn onWait(ctx: *anyopaque, ms: i64, abortable: bool) bool {
        const self: *Rig = @ptrCast(@alignCast(ctx));
        self.close();
        self.line("~wait {d}{s}\n", .{ ms, if (abortable) "" else " hold" });
        self.now_ns += @as(i96, ms) * std.time.ns_per_ms;
        return false; // no key is ever pressed mid-effect
    }

    /// Time passing with nothing on screen moving (the quiet between two bursts of output).
    pub fn pause(self: *Rig, ms: i64) void {
        self.close();
        self.line("~pause {d}\n", .{ms});
        self.now_ns += @as(i96, ms) * std.time.ns_per_ms;
    }

    /// The idle prompt's beat: one skin frame later, through the console's own idle hook.
    pub fn idle(self: *Rig, beats: usize) void {
        for (0..beats) |_| {
            self.close();
            self.line("~idle {d}\n", .{skin.frame_ms});
            self.now_ns += skin.frame_ms * std.time.ns_per_ms;
            _ = hooks.idleTick(&self.idle_ctx);
        }
    }

    /// A labelled section of the golden (what the scenario does next).
    pub fn mark(self: *Rig, label: []const u8) void {
        self.close();
        self.line("# {s}\n", .{label});
    }

    /// Type `line` at the prompt: its echo is pinned as the scrollback text it adds (a plain append
    /// paints however the screen likes), then the command runs and everything it paints is pinned.
    pub fn typeLine(self: *Rig, typed: []const u8) !void {
        self.mark(typed);
        loop.echoCommand(&self.session, &self.scr, typed);
        self.frame.clearRetainingCapacity();
        self.dumpLines("echo", &.{self.scr.lines.back().?});
        _ = try console.handle(&self.session, self.io, typed);
    }

    /// Spell out lines of text (the header art, the scrollback) into the golden.
    pub fn dumpLines(self: *Rig, label: []const u8, lines: []const []u8) void {
        self.mark(label);
        for (lines) |l| {
            escape(&self.out, self.gpa, l, false);
            self.out.append(self.gpa, '\n') catch {};
        }
    }

    fn line(self: *Rig, comptime fmt: []const u8, args: anytype) void {
        self.out.print(self.gpa, fmt, args) catch {};
    }

    /// Close the frame in progress: its tick, its length and hash, and (when small) its bytes.
    fn close(self: *Rig) void {
        const bytes = stripWrappers(self.frame.items);
        const t = @divTrunc(self.now_ns, std.time.ns_per_ms) - start_ms;
        if (bytes.len != 0) {
            self.line("@{d} {d}B {x:0>16}\n", .{ t, bytes.len, std.hash.Wyhash.hash(0, bytes) });
            if (bytes.len <= inline_max) {
                escape(&self.out, self.gpa, bytes, true);
                self.out.append(self.gpa, '\n') catch {};
            }
        }
        self.frame.clearRetainingCapacity();
    }

    /// Compare what was recorded with the golden, or record it under STENCIL_UPDATE_FX_PINS.
    pub fn expectPin(self: *Rig, name: []const u8) !void {
        self.close();
        const io = self.threaded.io();
        var pbuf: [128]u8 = undefined;
        const path = try std.fmt.bufPrint(&pbuf, "tests/pins/fx/{s}.txt", .{name});
        if (updating()) {
            std.Io.Dir.cwd().createDirPath(io, "tests/pins/fx") catch {};
            return std.Io.Dir.cwd().writeFile(io, .{ .sub_path = path, .data = self.out.items });
        }
        const want = std.Io.Dir.cwd().readFileAlloc(io, path, self.gpa, .limited(4 << 20)) catch |e| {
            std.debug.print("fx pin '{s}' unreadable ({s})\n", .{ path, @errorName(e) });
            return error.MissingPin;
        };
        defer self.gpa.free(want);
        if (std.mem.eql(u8, want, self.out.items)) return;
        var at: usize = 0;
        while (at < want.len and at < self.out.items.len and want[at] == self.out.items[at]) at += 1;
        const from = if (std.mem.lastIndexOfScalar(u8, want[0..at], '\n')) |n| n + 1 else 0;
        std.debug.print("\nfx pin mismatch: {s} at byte {d}\n- {s}\n+ {s}\n", .{ path, at, firstLine(want[from..]), firstLine(self.out.items[from..]) });
        return error.PinMismatch;
    }
};

fn firstLine(s: []const u8) []const u8 {
    const end = std.mem.indexOfScalar(u8, s, '\n') orelse s.len;
    return s[0..@min(end, 400)];
}

fn updating() bool {
    const v = std.c.getenv("STENCIL_UPDATE_FX_PINS") orelse return false;
    const s = std.mem.span(v);
    return s.len != 0 and !std.mem.eql(u8, s, "0");
}

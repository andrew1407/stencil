//! Test fixtures shared by the console's own suites (attachments.zig here, the
//! llm_prompt_*_test.zig files under cli/tests/): a logo-sink capture, a scratch session
//! and the tiny PNGs that stand in for uploads. Test-only — nothing in the app calls it.
const std = @import("std");
const image = @import("../../media/image.zig");
const server = @import("../../server/client.zig");
const logo = @import("../../app/logo.zig");
const llm = @import("../../llm.zig");
const layout_mod = @import("../../media/layout.zig");
const Session = @import("../session.zig").Session;
const plan = @import("plan.zig");
const applyPlanAction = plan.applyPlanAction;

/// Collects what the console TELLS the user (the logo.print sink the full-screen console
/// installs), so a test can assert on the notes an action printed.
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

/// applyPlanAction for the tests that drive ONE non-save action: a scratch io and a
/// fresh "no attachment adopted yet" state, so each case reads as the action itself.
pub fn applyOne(
    session: *Session,
    a: llm.Action,
    edited: *bool,
    steps: *std.ArrayList(layout_mod.FrameStep),
) bool {
    var threaded = std.Io.Threaded.init(session.gpa, .{});
    defer threaded.deinit();
    var active: ?usize = null;
    return applyPlanAction(session, threaded.io(), a, edited, steps, &active);
}

/// A quiet logo sink so the LLM helpers' notes don't leak into the test runner's output.
pub fn swallowPrint(_: *anyopaque, _: []const u8) void {}

/// A session over a fresh 4x4 image: white with a black left column (a real edge, so the
/// contour render differs from the original).
pub fn testSession(a: std.mem.Allocator) !Session {
    var session = Session{ .gpa = a };
    errdefer session.deinit();
    const px = try a.alloc(u8, 4 * 4 * 4);
    @memset(px, 255);
    for (0..4) |y| @memset(px[y * 16 ..][0..3], 0);
    try session.loadImage(.{ .width = 4, .height = 4, .pixels = px }, "test", true, .png, null);
    return session;
}


/// A solid `w`x`h` PNG, standing in for an uploaded file's encoded bytes.
pub fn pngOf(a: std.mem.Allocator, w: usize, h: usize) ![]u8 {
    const px = try a.alloc(u8, w * h * 4);
    @memset(px, 200);
    var img = image.Rgba8{ .width = w, .height = h, .pixels = px };
    defer img.deinit(a);
    return image.encode(a, img, .png);
}

/// A session over a 4x4 image with two uploads remembered as this turn's attachments:
/// "cat.png" (6x2) and "photos/dog.jpg" (3x5), in upload order.
pub fn attachedSession(a: std.mem.Allocator) !Session {
    var session = try testSession(a);
    errdefer session.deinit();
    try session.addAttachment("cat.png", try pngOf(a, 6, 2), .png, false);
    try session.addAttachment("photos/dog.jpg", try pngOf(a, 3, 5), .jpeg, false);
    return session;
}

/// A live-looking server.Client that never touched the network — enough for the
/// resolution/context paths, which only ever read `base` (and must never read `token`).
pub fn stubClient(a: std.mem.Allocator, io: std.Io, base: []const u8, token: []const u8) !server.Client {
    return .{
        .gpa = a,
        .io = io,
        .base = try a.dupe(u8, base),
        .token = try a.dupe(u8, token),
        .auth = try a.dupe(u8, "Bearer stub-token"),
        .credential = try a.dupe(u8, ""),
    };
}

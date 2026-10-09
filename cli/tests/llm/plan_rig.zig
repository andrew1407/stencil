//! One plan-action test's console: a capture of what it prints, a session over fixture.zig's
//! 4x4 image, and the edited flag and frame steps an action threads through. Set up in place
//! (the capture is the logo sink by address): `try rig.open(a);` then `defer rig.close();`.
const std = @import("std");
const logo = @import("../../src/app/logo.zig");
const layout_mod = @import("../../src/media/layout.zig");
const Session = @import("../../src/console/session.zig").Session;
const fixture = @import("../../src/console/llm/fixture.zig");

pub const PlanRig = struct {
    cap: fixture.Capture,
    session: Session,
    edited: bool,
    steps: std.ArrayList(layout_mod.FrameStep),

    pub fn open(self: *PlanRig, a: std.mem.Allocator) !void {
        self.cap = fixture.Capture.init(a);
        errdefer self.cap.deinit();
        self.cap.install();
        errdefer logo.clearSink();
        self.session = try fixture.testSession(a);
        self.edited = false;
        self.steps = .empty;
    }

    pub fn close(self: *PlanRig) void {
        self.steps.deinit(self.session.gpa);
        self.session.deinit();
        logo.clearSink();
        self.cap.deinit();
    }
};

//! The console suites' shared rig: the PNG fixture, the working image, and a capture of what the
//! user is told through `logo.print`.
const std = @import("std");
const console = @import("../../src/console.zig");
const image = @import("../../src/media/image.zig");

pub const sample = @embedFile("sample.png");

pub fn cur(session: *console.Session) image.Rgba8 {
    return session.current().*;
}

/// What the user is told through `logo.print`, captured by the console's own test fixture.
pub const Capture = @import("../../src/console/llm/fixture.zig").Capture;

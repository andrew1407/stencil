//! The LLM transport's watched wait, on net's one worker: a Ctrl-C mid-call stops the turn with a
//! note and the spinner beat it had, a reply that already landed is kept whatever the key says, and
//! the turn's own deadline ends a provider that never answers.
const std = @import("std");
const transport = @import("../../src/llm/transport.zig");
const wire = @import("../../src/llm/wire.zig");
const net = @import("../../src/net.zig");
const logo = @import("../../src/app/logo.zig");
const Capture = @import("../console/console_harness.zig").Capture;
const PostError = transport.PostError;
const Waiter = transport.Waiter;
const testing = std.testing;

var unsent_url = "http://example.invalid".*;
var unsent_body = "{}".*;
const unsent = wire.Request{ .url = &unsent_url, .body = &unsent_body };

/// A provider that never answers: it naps until the watch stops it.
fn silent(_: std.mem.Allocator, io: std.Io, _: []const u8, _: []const std.http.Header, _: []const u8) PostError!net.Response {
    io.sleep(.fromSeconds(20), .awake) catch return PostError.HttpFailed;
    return PostError.HttpFailed;
}

fn instant(gpa: std.mem.Allocator, _: std.Io, _: []const u8, _: []const std.http.Header, _: []const u8) PostError!net.Response {
    return .{ .status = 200, .body = try gpa.dupe(u8, "{\"ok\":true}") };
}

const Watch = struct {
    io: std.Io,
    presses: u8 = 0,
    beats: u8 = 0,
    press_on: u8 = 2, // the poll that reports a Ctrl-C, after its beat's wait (0 = never)
    fn poll(ctx: *anyopaque, timeout_ms: i32) bool {
        const self: *Watch = @ptrCast(@alignCast(ctx));
        self.presses += 1;
        self.io.sleep(.fromMilliseconds(timeout_ms), .awake) catch {};
        return self.press_on != 0 and self.presses >= self.press_on;
    }
    fn beat(ctx: *anyopaque, _: i64) void {
        const self: *Watch = @ptrCast(@alignCast(ctx));
        self.beats += 1;
    }
    fn waiter(self: *Watch, timeout_ms: i64) Waiter {
        return .{ .ctx = self, .poll = poll, .beat_ctx = self, .beat = beat, .timeout_ms = timeout_ms };
    }
};

test "postJson: a Ctrl-C mid-call stops the turn at once, with a note and the beat it had" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var watch = Watch{ .io = io };
    const t0 = std.Io.Clock.now(.awake, io).toMilliseconds();
    try testing.expectError(PostError.Cancelled, transport.postJsonVia(a, io, &unsent, watch.waiter(0), silent));
    try testing.expect(std.Io.Clock.now(.awake, io).toMilliseconds() - t0 < 5_000); // not the 20 s nap
    try testing.expectEqual(@as(u8, 2), watch.presses); // it really waited a beat before the press landed
    try testing.expectEqual(@as(u8, 1), watch.beats); // and the spinner got that one beat, not the cancelling one
    try testing.expect(std.mem.indexOf(u8, cap.text(), "cancelled — the assistant turn was stopped") != null);
}

test "postJson: a reply that lands first is collected, cancel or not" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    // The press ends the very first beat — by which time the instant answer has landed.
    var watch = Watch{ .io = io, .press_on = 1 };
    const body = try transport.postJsonVia(a, io, &unsent, watch.waiter(0), instant);
    defer a.free(body);
    try testing.expectEqualStrings("{\"ok\":true}", body);
}

test "postJson: the turn's deadline ends a provider that never answers" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var watch = Watch{ .io = io, .press_on = 0 };
    try testing.expectError(PostError.TimedOut, transport.postJsonVia(a, io, &unsent, watch.waiter(1000), silent));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "the LLM endpoint did not answer within 1s") != null);
}

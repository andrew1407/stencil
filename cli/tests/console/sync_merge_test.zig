// The co-edit conflict: a layout PUT that meets a 409 re-reads the project, unions the peer's
// lines with ours through core and takes its filter unless ours changed (as the browser's
// mergePeer does), records that as one state and retries at its version, over a faked transport.
const std = @import("std");
const console = @import("../../src/console.zig");
const remoteEvents = @import("../../src/console/remoteEvents.zig");
const server = @import("../../src/server/client.zig");
const harness = @import("console_harness.zig");
const logo = @import("../../src/app/logo.zig");
const testing = std.testing;

const shared = "{\"points\":[{\"x\":1,\"y\":1},{\"x\":9,\"y\":1}],\"color\":\"#00f\"}";
const peer = "{\"points\":[{\"x\":2,\"y\":5},{\"x\":8,\"y\":5}],\"color\":\"#f00\"}";
const mine = "{\"points\":[{\"x\":3,\"y\":7},{\"x\":7,\"y\":7}],\"color\":\"#0f0\"}";

const Fake = struct {
    var puts: usize = 0;
    var put_body: std.ArrayList(u8) = .empty;
    var project: []const u8 = "";
};

fn transport(gpa: std.mem.Allocator, _: std.Io, _: []const u8, method: std.http.Method, payload: ?[]const u8, _: []const std.http.Header) server.TransportError![]u8 {
    if (method == .PUT) {
        Fake.puts += 1;
        if (Fake.puts == 1) return server.Error.Conflict;
        Fake.put_body.clearRetainingCapacity();
        try Fake.put_body.appendSlice(testing.allocator, payload orelse "");
    }
    return gpa.dupe(u8, if (method == .GET) Fake.project else "{}");
}

/// A blank session linked to project p1 at version 3, drawing `lines`, its server faked.
fn linked(a: std.mem.Allocator, io: std.Io, lines: []const u8) !console.Session {
    var session = console.Session{ .gpa = a };
    errdefer session.deinit();
    _ = try console.handle(&session, io, "/blank 64 48 white");
    try session.setRemote("http://localhost:8090", "p1");
    try session.servers.append(a, .{
        .gpa = a,
        .io = io,
        .base = try a.dupe(u8, "http://localhost:8090"),
        .token = try a.dupe(u8, "t"),
        .auth = try a.dupe(u8, "Bearer t"),
        .credential = try a.dupe(u8, ""),
        .transport = transport,
    });
    try session.setLines(lines);
    session.remote_version = 3;
    Fake.puts = 0;
    return session;
}

/// The io and the captured console output one test's session runs under; it must not move.
const Rig = struct {
    threaded: std.Io.Threaded,
    cap: harness.Capture,

    fn start(self: *Rig, a: std.mem.Allocator) void {
        self.* = .{ .threaded = std.Io.Threaded.init(a, .{}), .cap = harness.Capture.init(a) };
        self.cap.install();
    }

    fn stop(self: *Rig) void {
        logo.clearSink();
        self.cap.deinit();
        self.threaded.deinit();
        Fake.put_body.deinit(testing.allocator);
        Fake.put_body = .empty;
    }
};

fn put(needle: []const u8) bool {
    return std.mem.indexOf(u8, Fake.put_body.items, needle) != null;
}

test "sync: a 409 merges the peer's lines with ours as one state, then retries at its version" {
    const a = testing.allocator;
    var rig: Rig = undefined;
    rig.start(a);
    defer rig.stop();

    Fake.project = "{\"project\":{\"id\":\"p1\",\"version\":7},\"layout\":{\"lines\":[" ++ peer ++ "," ++ shared ++ "]}}";
    var session = try linked(a, rig.threaded.io(), "{\"lines\":[" ++ shared ++ "," ++ mine ++ "]}");
    defer session.deinit();
    const states = session.stateCount();

    remoteEvents.pushResult(&session);
    try testing.expectEqual(@as(usize, 2), Fake.puts);
    try testing.expectEqual(states + 1, session.stateCount());
    try testing.expectEqualStrings("[" ++ peer ++ "," ++ shared ++ "," ++ mine ++ "]", session.state().lines());
    try testing.expect(std.mem.endsWith(u8, Fake.put_body.items, "\"version\":7}"));
    try testing.expect(put(peer ++ "," ++ shared ++ "," ++ mine));
    try testing.expectEqual(@as(i64, 7), session.remote_version);
    try testing.expect(std.mem.indexOf(u8, rig.cap.text(), "synced to http://localhost:8090") != null);

    // /undo takes the merge back to our own lines.
    try testing.expect(session.undo());
    try testing.expectEqualStrings("[" ++ shared ++ "," ++ mine ++ "]", session.state().lines());
}

test "sync: a peer that added no line or filter of its own leaves ours and records nothing" {
    const a = testing.allocator;
    var rig: Rig = undefined;
    rig.start(a);
    defer rig.stop();

    Fake.project = "{\"project\":{\"id\":\"p1\",\"version\":9},\"layout\":{\"lines\":[" ++ shared ++ "]}}";
    var session = try linked(a, rig.threaded.io(), "{\"lines\":[" ++ shared ++ "," ++ mine ++ "]}");
    defer session.deinit();
    const states = session.stateCount();

    remoteEvents.pushResult(&session);
    try testing.expectEqual(@as(usize, 2), Fake.puts);
    try testing.expectEqual(states, session.stateCount());
    try testing.expectEqualStrings("[" ++ shared ++ "," ++ mine ++ "]", session.state().lines());
    try testing.expect(std.mem.endsWith(u8, Fake.put_body.items, "\"version\":9}"));
}

test "sync: a 409 takes the peer's filter while ours is unchanged since the last push" {
    const a = testing.allocator;
    var rig: Rig = undefined;
    rig.start(a);
    defer rig.stop();

    Fake.project = "{\"project\":{\"id\":\"p1\",\"version\":4},\"layout\":{\"imageFilter\":\"custom\",\"filterColor\":\"#7c3aed\",\"lines\":[" ++ shared ++ "]}}";
    var session = try linked(a, rig.threaded.io(), "{\"lines\":[" ++ shared ++ "]}");
    defer session.deinit();
    const states = session.stateCount();
    try testing.expect(!session.filter_dirty);

    remoteEvents.pushResult(&session);
    try testing.expectEqual(states + 1, session.stateCount());
    try testing.expectEqualStrings("custom", session.state().filter_mode);
    try testing.expectEqualStrings("#7c3aed", session.state().filter_color);
    try testing.expectEqualStrings("[" ++ shared ++ "]", session.state().lines());
    try testing.expect(put("\"imageFilter\":\"custom\",\"filterColor\":\"#7c3aed\""));
}

test "sync: a 409 keeps our filter when it changed since the last push, and the push settles it" {
    const a = testing.allocator;
    var rig: Rig = undefined;
    rig.start(a);
    defer rig.stop();

    Fake.project = "{\"project\":{\"id\":\"p1\",\"version\":5},\"layout\":{\"imageFilter\":\"bw\",\"lines\":[" ++ peer ++ "]}}";
    var session = try linked(a, rig.threaded.io(), "{\"lines\":[" ++ mine ++ "]}");
    defer session.deinit();
    _ = try console.handle(&session, rig.threaded.io(), "/filter sepia");
    try testing.expect(session.filter_dirty);
    const states = session.stateCount();

    remoteEvents.pushResult(&session);
    try testing.expectEqual(states + 1, session.stateCount()); // the peer's line, and only that
    try testing.expectEqualStrings("sepia", session.state().filter_mode);
    try testing.expectEqualStrings("[" ++ peer ++ "," ++ mine ++ "]", session.state().lines());
    try testing.expect(put("\"imageFilter\":\"sepia\""));
    try testing.expect(!session.filter_dirty);
}

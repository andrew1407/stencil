//! Server verbs with no server through console.handle: a graceful no-op, and "there are none"
//! as an answer to a listing but a refusal of an action.
const std = @import("std");
const console = @import("../../src/console.zig");
const logo = @import("../../src/app/logo.zig");
const Capture = @import("console_harness.zig").Capture;
const testing = std.testing;

test "console: /project-color without an active server project is a graceful no-op" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var session = console.Session{ .gpa = a };
    defer session.deinit();

    // No connection / no fetched project: both the show and set forms report an error and
    // return false (the session keeps running), never touching the network or crashing.
    try testing.expect(!try console.handle(&session, io, "/project-color"));
    try testing.expect(!try console.handle(&session, io, "/project-color #ff5623"));
    try testing.expect(!session.hasRemote());
}

test "console: /keywords commands without a server connection are graceful no-ops" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var session = console.Session{ .gpa = a };
    defer session.deinit();

    // With no connected server, every keyword command reports "no server connections" and
    // returns false (the session keeps running), never touching the network or crashing.
    try testing.expect(!try console.handle(&session, io, "/keywords MyProject"));
    try testing.expect(!try console.handle(&session, io, "/keywords-search cat dog"));
    try testing.expect(!try console.handle(&session, io, "/keywords-add MyProject cat"));
    try testing.expect(!try console.handle(&session, io, "/keywords-del [\"a\",\"b\"] cat dog"));
    // Bare add/del (missing args) also stay clean.
    try testing.expect(!try console.handle(&session, io, "/keywords-add"));
    try testing.expect(!session.hasRemote());
}

test "console: 'there are none' is an answer for a listing, a refusal for an action" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var session = console.Session{ .gpa = a };
    defer session.deinit();

    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();

    // A listing command truthfully answering "there are none" carries no severity…
    _ = try console.handle(&session, io, "/connections");
    try testing.expectEqualStrings("no server connections — use '/connect <url>'\n", cap.text());

    // …while a command that tried to act and could not is an error, same wording.
    cap.buf.clearRetainingCapacity();
    _ = try console.handle(&session, io, "/disconnect");
    try testing.expectEqualStrings("error: no server connections — use '/connect <url>'\n", cap.text());
}

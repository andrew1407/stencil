//! The console-plan server ops: connect, disconnect and reconnect resolved against this session's
//! own servers (§10 stance), and the console context a turn carries (URLs ride, tokens never do).
const std = @import("std");
const logo = @import("../../src/app/logo.zig");
const fixture = @import("../../src/console/llm/fixture.zig");
const stubClient = fixture.stubClient;
const swallowPrint = fixture.swallowPrint;
const testSession = fixture.testSession;
const config = @import("../../src/console/llm/config.zig");
const plan = @import("../../src/console/llm/plan.zig");
const applyPlanAction = plan.applyPlanAction;
const consoleContext = config.consoleContext;
const PlanRig = @import("plan_rig.zig").PlanRig;
const testing = std.testing;

test "plan connect/disconnect resolve only the user's own servers; misses are notes (§10 stance)" {
    const a = testing.allocator;
    var rig: PlanRig = undefined;
    try rig.open(a);
    defer rig.close();
    const cap = &rig.cap;
    const session = &rig.session;
    const edited = &rig.edited;
    const steps = &rig.steps;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();

    // This session connected to two alpha ports; only :8090 is still live.
    try session.rememberServer("http://alpha.example:8090");
    try session.rememberServer("http://alpha.example:9091");
    try session.servers.append(a, try stubClient(a, io, "http://alpha.example:8090", "sekrit-token"));

    var active: ?usize = null;

    // A resolved connect to a live server is a no-op note — no fresh handshake.
    const live = try a.dupe(u8, "http://alpha.example:8090");
    defer a.free(live);
    try testing.expect(applyPlanAction(session, io, .{ .connect = .{ .server = live } }, edited, steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "already connected to http://alpha.example:8090") != null);

    // A bare host both known entries share is ambiguous; an unknown host is the user's
    // to /connect — both are notes, and the plan (returns true) carries on.
    try testing.expect(applyPlanAction(session, io, .{ .connect = .{ .server = "alpha.example" } }, edited, steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "matches several of this session's servers") != null);
    try testing.expect(applyPlanAction(session, io, .{ .connect = .{ .server = "http://evil.example" } }, edited, steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "not a server you connected this session; run '/connect <url>' yourself") != null);
    try testing.expectEqual(@as(usize, 1), session.servers.items.len); // nothing new appeared

    // Disconnect resolves against LIVE connections: the bare host is unique there.
    try testing.expect(applyPlanAction(session, io, .{ .disconnect = .{ .server = "alpha.example" } }, edited, steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "disconnected from http://alpha.example:8090") != null);
    try testing.expectEqual(@as(usize, 0), session.servers.items.len);
    // …and a second disconnect finds nothing to drop — a note, not a failure.
    try testing.expect(applyPlanAction(session, io, .{ .disconnect = .{ .server = "alpha.example" } }, edited, steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "skipped disconnect — not connected to \"alpha.example\"") != null);
    // The known pool survives the disconnect: the user could ask to reconnect later.
    try testing.expectEqual(@as(usize, 2), session.known_servers.items.len);
    try testing.expect(!edited.*);
}

test "console context suffix: URLs + active project ride, the token never does" {
    const a = testing.allocator;
    var quiet: u8 = 0;
    logo.setSink(swallowPrint, &quiet);
    defer logo.clearSink();
    var session = try testSession(a);
    defer session.deinit();
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();

    try session.servers.append(a, try stubClient(a, threaded.io(), "http://ctx.example:8090", "sekrit-token"));
    try session.rememberServer("http://ctx.example:8090");
    try session.setRemote("http://ctx.example:8090", "p_1");
    try session.setLabel("portrait");

    var arena = std.heap.ArenaAllocator.init(a);
    defer arena.deinit();
    // with_projects=false — the offline half; the fetch only ever ADDS project names.
    const ctx = try consoleContext(&session, arena.allocator(), false);
    try testing.expect(std.mem.indexOf(u8, ctx, "http://ctx.example:8090 (active project's server)") != null);
    try testing.expect(std.mem.indexOf(u8, ctx, "Active server project: \"portrait\".") != null);
    try testing.expect(std.mem.indexOf(u8, ctx, "Projects on") == null); // not fetched ≠ empty
    try testing.expect(std.mem.indexOf(u8, ctx, "sekrit-token") == null); // never a token
    try testing.expect(std.mem.indexOf(u8, ctx, "Bearer") == null);
}

test "plan reconnect resolves like connect and takes the /reconnect path (§10)" {
    const a = testing.allocator;
    var rig: PlanRig = undefined;
    try rig.open(a);
    defer rig.close();
    const cap = &rig.cap;
    const session = &rig.session;
    const edited = &rig.edited;
    const steps = &rig.steps;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();

    // The pool a plan reconnect resolves against is the servers /connect-ed this session; one OTHER live
    // connection keeps /reconnect from its no-connections early-out without any network involved.
    try session.rememberServer("http://a.example:1");
    try session.rememberServer("http://a.example:2");
    try session.rememberServer("http://b.example:9");
    try session.servers.append(a, try stubClient(a, io, "http://c.example:1", "tok"));

    var active: ?usize = null;

    // A unique host resolves (connect's rule) and reaches doReconnect — which notes a
    // match that is not currently live, exactly like the typed /reconnect would.
    try testing.expect(applyPlanAction(session, io, .{ .reconnect = .{ .server = "b.example" } }, edited, steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "not connected to http://b.example:9") != null);
    // Ambiguous and unknown names are notes, never failed plans (§10 stance).
    try testing.expect(applyPlanAction(session, io, .{ .reconnect = .{ .server = "a.example" } }, edited, steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "skipped reconnect — \"a.example\" matches several of this session's servers") != null);
    try testing.expect(applyPlanAction(session, io, .{ .reconnect = .{ .server = "http://evil.example" } }, edited, steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "skipped reconnect — \"http://evil.example\" is not a server you connected this session") != null);
    try testing.expectEqual(@as(usize, 1), session.servers.items.len); // nothing new appeared
    try testing.expect(!edited.*);
}

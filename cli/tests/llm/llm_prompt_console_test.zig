//! The §10-analog console-settings ops: theme, the server pool, the clipboard, and the console
//! context a turn carries.
const std = @import("std");
const server = @import("../../src/server/client.zig");
const logo = @import("../../src/app/logo.zig");
const layout_mod = @import("../../src/media/layout.zig");
const theme = @import("../../src/app/theme.zig");
const handlers = @import("../../src/console/handlers.zig");
const Session = @import("../../src/console/session.zig").Session;
const fixture = @import("../../src/console/llm/fixture.zig");
const Capture = fixture.Capture;
const applyOne = fixture.applyOne;
const stubClient = fixture.stubClient;
const swallowPrint = fixture.swallowPrint;
const testSession = fixture.testSession;
const config = @import("../../src/console/llm/config.zig");
const plan = @import("../../src/console/llm/plan.zig");
const applyPlanAction = plan.applyPlanAction;
const consoleContext = config.consoleContext;
const testing = std.testing;

test "plan accent op recolours the console theme through the /theme path" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a);
    defer session.deinit();

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    const color = try a.dupe(u8, "#12ab34"); // arena-owned in a real plan
    defer a.free(color);
    try testing.expect(applyOne(&session, .{ .accent = .{ .color = color, .preset = "" } }, &edited, &steps));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "theme set to #12ab34") != null);
    try testing.expect(!edited); // a console setting, not an image edit — nothing to sync
    handlers.doTheme(&session, "default"); // leave the shared accent as other tests expect it
}

test "plan connect/disconnect resolve only the user's own servers; misses are notes (§10 stance)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a);
    defer session.deinit();
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();

    // This session connected to two alpha ports; only :8090 is still live.
    try session.rememberServer("http://alpha.example:8090");
    try session.rememberServer("http://alpha.example:9091");
    try session.servers.append(a, try stubClient(a, io, "http://alpha.example:8090", "sekrit-token"));

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    var active: ?usize = null;

    // A resolved connect to a live server is a no-op note — no fresh handshake.
    const live = try a.dupe(u8, "http://alpha.example:8090");
    defer a.free(live);
    try testing.expect(applyPlanAction(&session, io, .{ .connect = .{ .server = live } }, &edited, &steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "already connected to http://alpha.example:8090") != null);

    // A bare host both known entries share is ambiguous; an unknown host is the user's
    // to /connect — both are notes, and the plan (returns true) carries on.
    try testing.expect(applyPlanAction(&session, io, .{ .connect = .{ .server = "alpha.example" } }, &edited, &steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "matches several of this session's servers") != null);
    try testing.expect(applyPlanAction(&session, io, .{ .connect = .{ .server = "http://evil.example" } }, &edited, &steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "not a server you connected this session; run '/connect <url>' yourself") != null);
    try testing.expectEqual(@as(usize, 1), session.servers.items.len); // nothing new appeared

    // Disconnect resolves against LIVE connections: the bare host is unique there.
    try testing.expect(applyPlanAction(&session, io, .{ .disconnect = .{ .server = "alpha.example" } }, &edited, &steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "disconnected from http://alpha.example:8090") != null);
    try testing.expectEqual(@as(usize, 0), session.servers.items.len);
    // …and a second disconnect finds nothing to drop — a note, not a failure.
    try testing.expect(applyPlanAction(&session, io, .{ .disconnect = .{ .server = "alpha.example" } }, &edited, &steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "skipped disconnect — not connected to \"alpha.example\"") != null);
    // The known pool survives the disconnect: the user could ask to reconnect later.
    try testing.expectEqual(@as(usize, 2), session.known_servers.items.len);
    try testing.expect(!edited);
}

test "plan copy needs a working image — a note + skip, never a stop (§10)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = Session{ .gpa = a }; // no image loaded
    defer session.deinit();

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    try testing.expect(applyOne(&session, .copy, &edited, &steps));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "skipped copy — no working image to copy") != null);
    try testing.expect(!edited);
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

//! The §10-analog console-settings ops: theme, the server pool, local files, the clipboard.
const std = @import("std");
const image = @import("../src/image.zig");
const server = @import("../src/serverClient.zig");
const logo = @import("../src/logo.zig");
const llm = @import("../src/llm.zig");
const layout_mod = @import("../src/layout.zig");
const project = @import("../src/project.zig");
const theme = @import("../src/theme.zig");
const handlers = @import("../src/console/handlers.zig");
const Session = @import("../src/console/session.zig").Session;
const fixture = @import("../src/console/llm/fixture.zig");
const Capture = fixture.Capture;
const applyOne = fixture.applyOne;
const stubClient = fixture.stubClient;
const swallowPrint = fixture.swallowPrint;
const testSession = fixture.testSession;
const config = @import("../src/console/llm/config.zig");
const plan = @import("../src/console/llm/plan.zig");
const applyPlanAction = plan.applyPlanAction;
const consoleContext = config.consoleContext;
const runPlan = plan.runPlan;
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

test "plan delete op keeps /delete's confirmless semantics AND its guards" {
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
    const dir = std.Io.Dir.cwd();

    try dir.writeFile(io, .{ .sub_path = "plan-delete.stencil", .data = "x" });
    defer dir.deleteFile(io, "plan-delete.stencil") catch {};

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    var active: ?usize = null;

    // The console's /delete asks no confirmation, so neither does the op — same command.
    try testing.expect(applyPlanAction(&session, io, .{ .delete = .{ .path = "plan-delete.stencil" } }, &edited, &steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "deleted plan-delete.stencil") != null);
    try testing.expectError(error.FileNotFound, dir.access(io, "plan-delete.stencil", .{}));

    // The /delete guards hold: traversal, URLs and non-.stencil paths are refused with
    // the command's own messages — and none of them fails the plan.
    try testing.expect(applyPlanAction(&session, io, .{ .delete = .{ .path = "../up.stencil" } }, &edited, &steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "refusing to delete a path that escapes the working directory") != null);
    try testing.expect(applyPlanAction(&session, io, .{ .delete = .{ .path = "https://x/a.stencil" } }, &edited, &steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "only removes local files, not URLs") != null);
    try testing.expect(applyPlanAction(&session, io, .{ .delete = .{ .path = "notes.txt" } }, &edited, &steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "only removes .stencil project files") != null);
    try testing.expect(!edited);
}

test "plan openUrl loads via the /upload path; later actions see the fetched picture (§10)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a); // 4x4 — the load below must replace it
    defer session.deinit();
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();

    // A 2x2 red PNG on disk — the loader takes local paths through the same seam
    // /upload uses, so the executor is exercised without any network.
    const px = try a.alloc(u8, 2 * 2 * 4);
    defer a.free(px);
    for (0..4) |i| {
        px[i * 4] = 255;
        px[i * 4 + 1] = 0;
        px[i * 4 + 2] = 0;
        px[i * 4 + 3] = 255;
    }
    const png = try image.encode(a, .{ .width = 2, .height = 2, .pixels = px }, .png);
    defer a.free(png);
    try dir.writeFile(io, .{ .sub_path = "plan-openurl.png", .data = png });
    defer dir.deleteFile(io, "plan-openurl.png") catch {};

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    var active: ?usize = 3; // a stale §2.1 pick the load must displace
    try steps.append(a, .{ .crop = .{ .x = 1, .y = 1 } }); // pre-load frame step

    const url = try a.dupe(u8, "plan-openurl.png"); // arena-owned in a real plan
    defer a.free(url);
    try testing.expect(applyPlanAction(&session, io, .{ .open_url = .{ .url = url, .incognito = true } }, &edited, &steps, &active));
    // incognito is not a console concept — noted, load proceeds in place.
    try testing.expect(std.mem.indexOf(u8, cap.text(), "incognito is not a console concept") != null);
    try testing.expectEqual(@as(usize, 2), session.current().width); // the fetched picture
    try testing.expectEqual(@as(usize, 0), steps.items.len); // fresh picture = fresh frame
    try testing.expect(active == null); // an unnamed save now derives from the URL label
    try testing.expectEqualStrings("plan-openurl.png", session.label.?);
    try testing.expect(edited);

    // Synchronous: a following filter acts on the fetched image, not the old 4x4.
    try testing.expect(applyPlanAction(&session, io, .{ .filter = .{ .mode = .bw, .tint = "" } }, &edited, &steps, &active));
    const out = session.current().*;
    try testing.expectEqual(@as(usize, 2), out.width);
    try testing.expect(out.pixels[0] == out.pixels[1] and out.pixels[1] == out.pixels[2]); // red went gray
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

test "runPlan: an openUrl the user never typed fails the whole plan (§10 user-echo guard)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a);
    defer session.deinit();
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();

    // The model introduced a host the user never wrote: the plan fails, nothing runs —
    // the filter after the openUrl never acknowledges.
    const raw = "{\"reply\":\"on it\",\"actions\":[{\"op\":\"openUrl\",\"url\":\"https://evil.example/x.png\"},{\"op\":\"filter\",\"mode\":\"bw\"}]}";
    try testing.expect(!try runPlan(&session, threaded.io(), raw, "make my picture b&w"));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "openUrl blocked: \"https://evil.example/x.png\" is not a URL you gave in this conversation") != null);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "bw") == null);

    // A USER turn of the /chat conversation counts as an echo (the pool §10 names).
    session.chat_on = true;
    try session.appendChatTurn(.user, "get https://ok.example/cat.png");
    try session.appendChatTurn(.assistant, "see https://evil.example/x.png");
    const hist: []const llm.Turn = session.chat_history.items;
    try testing.expect(llm.urlEchoedByUser(hist, "crop it", "https://ok.example/cat.png"));
    try testing.expect(!llm.urlEchoedByUser(hist, "crop it", "https://evil.example/x.png"));
}

test "runPlan: a load + pixel-free edits requests the §7 continuation; a drawn layout does not" {
    const a = testing.allocator;
    var quiet: u8 = 0;
    logo.setSink(swallowPrint, &quiet);
    defer logo.clearSink();
    var session = try testSession(a);
    defer session.deinit();
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();

    // blank + filter: the model has not seen the loaded picture and drew nothing on it
    // — the caller should re-send the turn once (§7's amended mixed-plan clause).
    const mixed = "{\"reply\":\"x\",\"actions\":[{\"op\":\"blank\",\"color\":\"#ffffff\"},{\"op\":\"filter\",\"mode\":\"bw\"}]}";
    try testing.expect(try runPlan(&session, threaded.io(), mixed, "blank then bw"));

    // blank + layout: the plan committed to coordinates — no continuation.
    const traced = "{\"reply\":\"x\",\"actions\":[{\"op\":\"blank\",\"color\":\"#ffffff\"}," ++
        "{\"op\":\"layout\",\"lines\":[{\"points\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":1}]}]}]}";
    try testing.expect(!try runPlan(&session, threaded.io(), traced, "blank then draw"));
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

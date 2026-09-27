//! The expanded console op set, the settings half: §10 crop album and §2 custom page dims,
//! formulas, the accent preset, and a reconnect resolved like /connect.
const std = @import("std");
const server = @import("../../src/server/client.zig");
const logo = @import("../../src/app/logo.zig");
const core = @import("../../src/core.zig");
const layout_mod = @import("../../src/media/layout.zig");
const theme = @import("../../src/app/theme.zig");
const handlers = @import("../../src/console/handlers.zig");
const fixture = @import("../../src/console/llm/fixture.zig");
const Capture = fixture.Capture;
const applyOne = fixture.applyOne;
const stubClient = fixture.stubClient;
const swallowPrint = fixture.swallowPrint;
const testSession = fixture.testSession;
const plan = @import("../../src/console/llm/plan.zig");
const applyPlanAction = plan.applyPlanAction;
const testing = std.testing;

test "plan crop album derives the missing axis from the page, landscape (§10)" {
    const a = testing.allocator;
    var quiet: u8 = 0;
    logo.setSink(swallowPrint, &quiet);
    defer logo.clearSink();

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);

    // x edges given, y missing → album derives the height from the LANDSCAPE page
    // aspect (A4 → 29.7/21 ≈ 1.414): 4px wide → round(4 / 1.414) = 3 high.
    var album_session = try testSession(a); // 4x4
    defer album_session.deinit();
    try testing.expect(applyOne(&album_session, .{ .crop = .{ .x1 = "0px", .x2 = "4px", .album = true } }, &edited, &steps));
    try testing.expectEqual(@as(usize, 4), album_session.current().width);
    try testing.expectEqual(@as(usize, 3), album_session.current().height);

    // Without album the portrait aspect derives a TALLER height (clamped to the image).
    steps.clearRetainingCapacity();
    var portrait_session = try testSession(a);
    defer portrait_session.deinit();
    try testing.expect(applyOne(&portrait_session, .{ .crop = .{ .x1 = "0px", .x2 = "4px" } }, &edited, &steps));
    try testing.expectEqual(@as(usize, 4), portrait_session.current().height);
}

test "plan custom page and blank cm dims land as the session's custom page (§2)" {
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

    // The custom page form: the /format custom path (name + cm dims).
    try testing.expect(applyOne(&session, .{ .page = .{ .format = "", .width = 10, .height = 20 } }, &edited, &steps));
    try testing.expectEqualStrings("custom", session.page_size);
    try testing.expectEqual(@as(f64, 10), session.custom_page_w);
    try testing.expectEqual(@as(f64, 20), session.custom_page_h);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "page format set to custom") != null);
    try testing.expect(edited); // rides the saved/synced layout, like /format

    // Blank dims size the page exactly like '/blank <w> <h>' (the shared 96-dpi
    // derivation) and override the format; the page pick follows the dims.
    const expect_px = core.defaultBlankSizePx(10, 5, 96.0);
    try testing.expect(applyOne(&session, .{ .blank = .{ .color = "#ffffff", .format = "a4", .width = 10, .height = 5 } }, &edited, &steps));
    try testing.expectEqual(@as(usize, @intCast(expect_px.w)), session.current().width);
    try testing.expectEqual(@as(usize, @intCast(expect_px.h)), session.current().height);
    try testing.expectEqualStrings("custom", session.page_size);
    try testing.expectEqual(@as(f64, 10), session.custom_page_w);
    try testing.expectEqual(@as(f64, 5), session.custom_page_h);
}

test "plan formula enabled:false restores identity keeping expressions; empty expr clears the axis (§2)" {
    const a = testing.allocator;
    var quiet: u8 = 0;
    logo.setSink(swallowPrint, &quiet);
    defer logo.clearSink();
    var session = try testSession(a);
    defer session.deinit();

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);

    try testing.expect(applyOne(&session, .{ .formula = .{ .axis = 'x', .expr = "x*2" } }, &edited, &steps));
    try testing.expect(session.allow_formulas);
    try testing.expectEqualStrings("x*2", session.formula_x);

    // OFF restores identity but keeps the expression (the /formula off semantics)…
    try testing.expect(applyOne(&session, .{ .formula = .{ .axis = 0, .expr = "", .enabled = false } }, &edited, &steps));
    try testing.expect(!session.allow_formulas);
    try testing.expectEqualStrings("x*2", session.formula_x);
    // …and ON brings it back.
    try testing.expect(applyOne(&session, .{ .formula = .{ .axis = 0, .expr = "", .enabled = true } }, &edited, &steps));
    try testing.expect(session.allow_formulas);

    // An empty expr clears exactly that axis.
    try testing.expect(applyOne(&session, .{ .formula = .{ .axis = 'x', .expr = "" } }, &edited, &steps));
    try testing.expectEqualStrings("", session.formula_x);
}

test "plan accent preset resolves through /theme's name table (§10)" {
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
    const preset = try a.dupe(u8, "green"); // arena-owned in a real plan
    defer a.free(preset);
    try testing.expect(applyOne(&session, .{ .accent = .{ .color = "", .preset = preset } }, &edited, &steps));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "theme set to green (#047857)") != null);
    // An unknown preset is /theme's own note + skip — the plan carries on.
    try testing.expect(applyOne(&session, .{ .accent = .{ .color = "", .preset = "frobnicate" } }, &edited, &steps));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "unknown theme 'frobnicate'") != null);
    try testing.expect(!edited);
    handlers.doTheme(&session, "default"); // leave the shared accent as other tests expect it
}

test "plan reconnect resolves like connect and takes the /reconnect path (§10)" {
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

    // The pool a plan reconnect resolves against is the servers /connect-ed this session; one OTHER live
    // connection keeps /reconnect from its no-connections early-out without any network involved.
    try session.rememberServer("http://a.example:1");
    try session.rememberServer("http://a.example:2");
    try session.rememberServer("http://b.example:9");
    try session.servers.append(a, try stubClient(a, io, "http://c.example:1", "tok"));

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    var active: ?usize = null;

    // A unique host resolves (connect's rule) and reaches doReconnect — which notes a
    // match that is not currently live, exactly like the typed /reconnect would.
    try testing.expect(applyPlanAction(&session, io, .{ .reconnect = .{ .server = "b.example" } }, &edited, &steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "not connected to http://b.example:9") != null);
    // Ambiguous and unknown names are notes, never failed plans (§10 stance).
    try testing.expect(applyPlanAction(&session, io, .{ .reconnect = .{ .server = "a.example" } }, &edited, &steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "skipped reconnect — \"a.example\" matches several of this session's servers") != null);
    try testing.expect(applyPlanAction(&session, io, .{ .reconnect = .{ .server = "http://evil.example" } }, &edited, &steps, &active));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "skipped reconnect — \"http://evil.example\" is not a server you connected this session") != null);
    try testing.expectEqual(@as(usize, 1), session.servers.items.len); // nothing new appeared
    try testing.expect(!edited);
}

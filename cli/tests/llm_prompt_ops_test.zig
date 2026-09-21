//! The expanded console op set: §2 undo/redo/reset + custom dims, §10 clear/reconnect/…
const std = @import("std");
const image = @import("../src/media/image.zig");
const server = @import("../src/server/client.zig");
const logo = @import("../src/app/logo.zig");
const core = @import("../src/core.zig");
const llm = @import("../src/llm.zig");
const layout_mod = @import("../src/media/layout.zig");
const theme = @import("../src/app/theme.zig");
const handlers = @import("../src/console/handlers.zig");
const Session = @import("../src/console/session.zig").Session;
const fixture = @import("../src/console/llm/fixture.zig");
const Capture = fixture.Capture;
const applyOne = fixture.applyOne;
const stubClient = fixture.stubClient;
const swallowPrint = fixture.swallowPrint;
const testSession = fixture.testSession;
const plan = @import("../src/console/llm/plan.zig");
const variants = @import("../src/console/llm/variants.zig");
const applyPlanAction = plan.applyPlanAction;
const renderVariant = variants.renderVariant;
const testing = std.testing;

test "plan undo/redo step the session's own history; running out of steps is a note (§2)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a); // 4x4
    defer session.deinit();

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);

    // Two REAL session edits to step back through: crop to 3x4, then rotate to 4x3.
    try testing.expect(applyOne(&session, .{ .crop = .{ .x1 = "1px", .y1 = "0px" } }, &edited, &steps));
    try testing.expect(applyOne(&session, .{ .rotate = .{ .dir = .right, .times = 1 } }, &edited, &steps));
    try testing.expectEqual(@as(usize, 4), session.current().width);
    try testing.expectEqual(@as(usize, 3), session.current().height);

    // One undo steps one HISTORY entry back (the rotate), one redo re-applies it.
    try testing.expect(applyOne(&session, .{ .undo = .{ .steps = 1 } }, &edited, &steps));
    try testing.expectEqual(@as(usize, 3), session.current().width);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "undone") != null);
    try testing.expect(applyOne(&session, .{ .redo = .{ .steps = 1 } }, &edited, &steps));
    try testing.expectEqual(@as(usize, 4), session.current().width);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "redone") != null);

    // Asking for more steps than exist takes what is there and notes the shortfall (§2).
    try testing.expect(applyOne(&session, .{ .undo = .{ .steps = 20 } }, &edited, &steps));
    try testing.expectEqual(@as(usize, 4), session.current().width); // back at the original
    try testing.expectEqual(@as(usize, 4), session.current().height);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "only 2 of 20 undo step(s) were available") != null);

    // At the original an undo has nothing to step — the /undo command's own message.
    try testing.expect(applyOne(&session, .{ .undo = .{ .steps = 1 } }, &edited, &steps));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "nothing to undo (at the original)") != null);
}

test "plan reset drops every pending edit back to the original (§2)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a); // 4x4
    defer session.deinit();

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    try testing.expect(applyOne(&session, .{ .crop = .{ .x1 = "1px", .y1 = "0px" } }, &edited, &steps));
    try testing.expect(applyOne(&session, .{ .rotate = .{ .dir = .left, .times = 1 } }, &edited, &steps));

    try testing.expect(applyOne(&session, .reset, &edited, &steps));
    try testing.expectEqual(@as(usize, 4), session.current().width);
    try testing.expectEqual(@as(usize, 4), session.current().height);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "reset to original") != null);
    // The history was dropped, not stepped: there is nothing to redo now.
    try testing.expect(applyOne(&session, .{ .redo = .{ .steps = 1 } }, &edited, &steps));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "nothing to redo (at the latest edit)") != null);
}

test "plan clear drops the working image through the /drop path (§10)" {
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
    try testing.expect(applyOne(&session, .clear, &edited, &steps));
    try testing.expect(!session.hasImage()); // the editor is empty, not a blank page
    try testing.expect(!edited); // a console-state change, not an image edit to sync
    // Cleared twice is /drop's own note — the plan carries on.
    try testing.expect(applyOne(&session, .clear, &edited, &steps));
    try testing.expect(std.mem.indexOf(u8, cap.text(), "no image loaded") != null);
}

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

test "plan layout with an empty lines array removes every drawn line (§4)" {
    const a = testing.allocator;
    var cap = Capture.init(a);
    defer cap.deinit();
    cap.install();
    defer logo.clearSink();
    var session = try testSession(a);
    defer session.deinit();
    try session.addLines("{\"lines\":[{\"points\":[{\"x\":0,\"y\":0},{\"x\":1,\"y\":1}],\"color\":\"#FF0000\"}]}");

    var edited = false;
    var steps: std.ArrayList(layout_mod.FrameStep) = .empty;
    defer steps.deinit(a);
    try testing.expect(applyOne(&session, .{ .layout = .{ .lines_json = "[]" } }, &edited, &steps));
    try testing.expectEqualStrings("[]", session.state().lines_json);
    try testing.expect(std.mem.indexOf(u8, cap.text(), "lines removed") != null);
    // Undoable, like the draw it removed.
    try testing.expect(session.undo());
    try testing.expect(std.mem.indexOf(u8, session.state().lines_json, "#FF0000") != null);
}

test "renderVariant: a filter op recolours the picture, not the lines already drawn" {
    const a = testing.allocator;
    var quiet: u8 = 0;
    logo.setSink(swallowPrint, &quiet);
    defer logo.clearSink();
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();

    // A red 16x12 picture with a green dot drawn on it, greyscaled by the variant: the
    // background turns grey, the annotation keeps its colour.
    var session = Session{ .gpa = a };
    defer session.deinit();
    const px = try a.alloc(u8, 16 * 12 * 4);
    core.fillRGBA(px, 16 * 12, .{ .r = 200, .g = 40, .b = 40, .a = 255 });
    try session.loadImage(.{ .width = 16, .height = 12, .pixels = px }, "test", true, .png, null);
    try session.addLines("{\"lines\":[{\"points\":[{\"x\":8,\"y\":6}],\"color\":\"#00FF00\",\"pointSize\":3,\"thickness\":2}]}");

    var actions = [_]llm.Action{.{ .filter = .{ .mode = .bw, .tint = "" } }};
    renderVariant(&session, io, .{ .label = "bw", .actions = &actions }, "bwtest", &.{});

    const dir = std.Io.Dir.cwd();
    defer dir.deleteFile(io, "variant-bwtest.png") catch {};
    const bytes = try dir.readFileAlloc(io, "variant-bwtest.png", a, .limited(1 << 20));
    defer a.free(bytes);
    var out = try image.decode(a, bytes);
    defer out.deinit(a);

    const dot = (6 * out.width + 8) * 4;
    try testing.expect(out.pixels[dot + 1] > 200); // green kept
    try testing.expect(out.pixels[dot] < 60 and out.pixels[dot + 2] < 60);
    const bg = (1 * out.width + 1) * 4;
    try testing.expectEqual(out.pixels[bg], out.pixels[bg + 1]);
    try testing.expectEqual(out.pixels[bg + 1], out.pixels[bg + 2]);
}

//! The console's exported layout JSON through console.handle: `/layout` writes the structured
//! lines, `/formula` values ride it once validated, and a cropRect is always named.
const std = @import("std");
const console = @import("../../src/console.zig");
const core = @import("../../src/core.zig");
const edits = @import("../../src/console/session/edits.zig");
const sample = @import("console_harness.zig").sample;
const testing = std.testing;

test "console: /layout exports the structured layout JSON to a file" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();

    const layout_in = "stencil_console_layout_in.json";
    const out = "stencil_console_layout.json";
    try dir.writeFile(io, .{
        .sub_path = layout_in,
        .data =
        \\{"lines":[{"points":[{"x":1,"y":2},{"x":3,"y":4}],"color":"#ff0000","width":2}]}
        ,
    });
    defer dir.deleteFile(io, layout_in) catch {};
    defer dir.deleteFile(io, out) catch {};

    var session = console.Session{ .gpa = a };
    defer session.deinit();

    // A blank page + a drawn layout, then export the layout JSON.
    _ = try console.handle(&session, io, "/blank 64 48 white");
    _ = try console.handle(&session, io, "/apply " ++ layout_in);
    _ = try console.handle(&session, io, "/layout " ++ out);

    const bytes = try dir.readFileAlloc(io, out, a, .limited(1 << 20));
    defer a.free(bytes);

    // It parses as JSON and carries the structured "lines" array.
    var parsed = try std.json.parseFromSlice(std.json.Value, a, bytes, .{});
    defer parsed.deinit();
    try testing.expect(parsed.value == .object);
    try testing.expect(parsed.value.object.get("lines") != null);
    try testing.expect(parsed.value.object.get("lines").? == .array);
    try testing.expect(parsed.value.object.get("lines").?.array.items.len >= 1);
}

test "console: /formula sets validated formulas that ride the exported layout" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();

    const out = "stencil_console_formula.json";
    defer dir.deleteFile(io, out) catch {};

    var session = console.Session{ .gpa = a };
    defer session.deinit();

    _ = try console.handle(&session, io, "/blank 64 48 white");
    _ = try console.handle(&session, io, "/formula x x*2 + 1");
    _ = try console.handle(&session, io, "/formula y y/3");
    // An invalid expression is rejected and leaves the prior value intact.
    _ = try console.handle(&session, io, "/formula x foo(x)");
    try testing.expectEqualStrings("x*2 + 1", session.formula_x);
    try testing.expectEqualStrings("y/3", session.formula_y);
    try testing.expect(session.allow_formulas);

    _ = try console.handle(&session, io, "/layout " ++ out);
    const bytes = try dir.readFileAlloc(io, out, a, .limited(1 << 20));
    defer a.free(bytes);
    var parsed = try std.json.parseFromSlice(std.json.Value, a, bytes, .{});
    defer parsed.deinit();
    const obj = parsed.value.object;
    try testing.expectEqualStrings("x*2 + 1", obj.get("formulaX").?.string);
    try testing.expectEqualStrings("y/3", obj.get("formulaY").?.string);
    try testing.expect(obj.get("allowFormulas").?.bool);
}

test "console: /formula reaches the page, the image and the other axis" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();

    var session = console.Session{ .gpa = a };
    defer session.deinit();

    // No image open: IMAGE_WIDTH is unsupplied, so the expression is refused outright rather
    // than silently resolving to zero.
    _ = try console.handle(&session, io, "/formula x IMAGE_WIDTH");
    try testing.expectEqualStrings("", session.formula_x);
    try testing.expect(!session.allow_formulas);

    _ = try console.handle(&session, io, "/blank 64 48 white");
    _ = try console.handle(&session, io, "/format a4");
    _ = try console.handle(&session, io, "/formula x IMAGE_WIDTH");
    try testing.expectEqualStrings("IMAGE_WIDTH", session.formula_x);
    _ = try console.handle(&session, io, "/formula y PAGE_WIDTH + PAGE_HEIGHT - x / 2");
    try testing.expectEqualStrings("PAGE_WIDTH + PAGE_HEIGHT - x / 2", session.formula_y);
    _ = try console.handle(&session, io, "/formula x 9"); // a constant needs no variable at all
    try testing.expectEqualStrings("9", session.formula_x);
    _ = try console.handle(&session, io, "/formula x PAGE_WIDTHS");
    try testing.expectEqualStrings("9", session.formula_x); // unknown name: the prior value stands

    // What those names resolve to. A 64x48 blank lays A4 on its side, and the console has no
    // display-unit switch, so PAGE_WIDTH reads cm until a caller names "in".
    const ctx = edits.formulaContext(&session);
    try testing.expectEqual(@as(f64, 64), core.applyFormulaCtx("IMAGE_WIDTH", 'x', 5, true, ctx));
    try testing.expectApproxEqAbs(@as(f64, 29.7), core.applyFormulaCtx("PAGE_WIDTH", 'x', 5, true, ctx), 1e-12);
    var inches = ctx;
    inches.unit = "in";
    try testing.expectApproxEqAbs(@as(f64, 29.7 / 2.54), core.applyFormulaCtx("PAGE_WIDTH", 'x', 5, true, inches), 1e-12);
    // Nothing supplied the live coordinates, so an axis name leaves the raw value alone.
    try testing.expectEqual(@as(f64, 5), core.applyFormulaCtx("x * 2", 'y', 5, true, ctx));
}

// An uncropped image must still name its cropRect in the layout: the GUIs auto-crop a freshly loaded
// image to the page aspect unless the layout names one, stranding lines outside the page rect.
test "console: layout always carries a cropRect, full-frame when nothing is cropped" {
    const a = testing.allocator;
    var threaded = std.Io.Threaded.init(a, .{});
    defer threaded.deinit();
    const io = threaded.io();
    const dir = std.Io.Dir.cwd();

    const in = "stencil_croprect_in.png";
    try dir.writeFile(io, .{ .sub_path = in, .data = sample });
    defer dir.deleteFile(io, in) catch {};

    var session = console.Session{ .gpa = a };
    defer session.deinit();
    _ = try console.handle(&session, io, "/upload " ++ in);

    // No crop applied → the whole 16x12 frame, stated explicitly.
    const bare = try session.currentLayoutJson();
    defer a.free(bare);
    try testing.expect(std.mem.indexOf(u8, bare, "\"cropRect\":{\"x\":0,\"y\":0,\"width\":16,\"height\":12}") != null);

    // Still full-frame after a quarter turn, in the ROTATED original's space (12x16).
    _ = try console.handle(&session, io, "/rotate 1");
    const turned = try session.currentLayoutJson();
    defer a.free(turned);
    try testing.expect(std.mem.indexOf(u8, turned, "\"cropRect\":{\"x\":0,\"y\":0,\"width\":12,\"height\":16}") != null);

    // An explicit crop still wins over the full-frame default.
    _ = try console.handle(&session, io, "/rotate 3"); // back to the upright original
    _ = try console.handle(&session, io, "/crop x1=0% x2=50% y1=0% y2=100%");
    const cropped = try session.currentLayoutJson();
    defer a.free(cropped);
    try testing.expect(std.mem.indexOf(u8, cropped, "\"cropRect\":{\"x\":0,\"y\":0,\"width\":8,\"height\":12}") != null);
}

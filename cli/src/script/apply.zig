//! One lowered op -> one edit on the canvas. Lengths are resolved here, against the image
//! as it stands right now, because a crop earlier in the block already changed its size.
const std = @import("std");

const core = @import("../core.zig");
const image = @import("../image.zig");
const layout_mod = @import("../layout.zig");
const pipeline = @import("../pipeline.zig");
const report = @import("../report.zig");
const scriptCore = @import("../scriptCore.zig");

pub const Error = error{ ScriptOpFailed, ScriptUndoUnsupported };

/// CSS pixels per cm at 96 dpi — the same basis the browser and the crop parser use.
const PX_PER_CM: f64 = 96.0 / 2.54;

fn dims(img: image.Rgba8) struct { w: f64, h: f64 } {
    return .{ .w = @floatFromInt(img.width), .h = @floatFromInt(img.height) };
}

/// Appends one line to the layout JSON the block is accumulating. Built as text because
/// that is what layout.parse already takes; the core supplies the resolved geometry.
fn appendLine(
    gpa: std.mem.Allocator,
    lines: *std.ArrayList(u8),
    pts: []const f64,
    color: []const u8,
    style: []const u8,
    fill: []const u8,
    point_color: []const u8,
    thickness: f64,
    point_size: f64,
    locked: bool,
) !void {
    var aw: std.Io.Writer.Allocating = .init(gpa);
    defer aw.deinit();
    const w = &aw.writer;
    if (lines.items.len > 0) try w.writeAll(",");
    try w.print("{{\"color\":\"{s}\",\"style\":\"{s}\",\"fillColor\":\"{s}\",\"pointColor\":\"{s}\"," ++
        "\"thickness\":{d},\"pointSize\":{d},\"locked\":{s},\"points\":[", .{
        color, style, fill, point_color, thickness, point_size, if (locked) "true" else "false",
    });
    var i: usize = 0;
    while (i + 1 < pts.len) : (i += 2) {
        if (i > 0) try w.writeAll(",");
        try w.print("{{\"x\":{d},\"y\":{d}}}", .{ pts[i], pts[i + 1] });
    }
    try w.writeAll("]}");
    try lines.appendSlice(gpa, aw.written());
}

/// Burns whatever `@line` / `@rect` / `@layout` accumulated into the pixels. Called before
/// every `@save` so the written file carries the marks.
pub fn flushLines(gpa: std.mem.Allocator, img: *image.Rgba8, lines: []const u8) !void {
    if (lines.len == 0) return;
    const doc = try std.fmt.allocPrint(gpa, "{{\"imageWidth\":{d},\"imageHeight\":{d},\"lines\":[{s}]}}", .{ img.width, img.height, lines });
    defer gpa.free(doc);
    var parsed = layout_mod.parse(gpa, doc) catch return Error.ScriptOpFailed;
    defer parsed.deinit();
    try pipeline.drawLayoutDoc(gpa, img, &parsed, null);
}

pub fn applyOp(
    gpa: std.mem.Allocator,
    io: std.Io,
    script: scriptCore.Script,
    index: u32,
    op: scriptCore.Op,
    img: *image.Rgba8,
    lines: *std.ArrayList(u8),
) !void {
    const d = dims(img.*);
    var buf: [2 * (scriptMaxPoints + 1)]f64 = undefined;

    switch (op.kind) {
        .crop => {
            const r = script.resolve(index, d.w, d.h, PX_PER_CM, PX_PER_CM, &buf) catch {
                report.err("line {d}: this crop resolves to nothing\n", .{op.line});
                return Error.ScriptOpFailed;
            };
            if (r.len < 4) return Error.ScriptOpFailed;
            // Marks are in the pre-crop frame, so burn them before the frame moves.
            try flushLines(gpa, img, lines.items);
            lines.clearRetainingCapacity();
            try pipeline.cropToRect(gpa, img, .{
                .x = @intFromFloat(@round(r[0])),
                .y = @intFromFloat(@round(r[1])),
                .w = @intFromFloat(@round(r[2])),
                .h = @intFromFloat(@round(r[3])),
            });
        },
        .filter => {
            const mode = script.opStr(index, 0);
            const tint = script.opStr(index, 1);
            pipeline.applyFilterMode(gpa, img, if (std.mem.eql(u8, mode, "custom")) tint else mode);
        },
        .line, .rect => {
            const r = script.resolve(index, d.w, d.h, PX_PER_CM, PX_PER_CM, &buf) catch {
                report.err("line {d}: this shape resolves to nothing\n", .{op.line});
                return Error.ScriptOpFailed;
            };
            if (r.len < 4) return Error.ScriptOpFailed;
            const pts = r[0 .. r.len - 2];
            try appendLine(gpa, lines, pts, script.opStr(index, 0), script.opStr(index, 1), script.opStr(index, 2), script.opStr(index, 3), r[r.len - 2], r[r.len - 1], op.kind == .rect);
        },
        .layout => {
            const src = script.opStr(index, 0);
            const mode = script.opStr(index, 1);
            var doc = pipeline.loadLayoutDoc(gpa, io, src) catch {
                report.err("line {d}: could not load the layout '{s}'\n", .{ op.line, src });
                return Error.ScriptOpFailed;
            };
            defer doc.deinit();
            // "replace" drops the marks accumulated so far; the pixels already burned in
            // by an earlier @save stay, exactly as they would in the editors.
            if (std.mem.eql(u8, mode, "replace")) lines.clearRetainingCapacity();
            try pipeline.drawLayoutDoc(gpa, img, &doc, null);
        },
        .undo, .redo => {
            // The lowerer resolves history statically: it re-emits the surviving edits
            // rather than asking an adapter to step a stack it does not have here.
            report.note("line {d}: @undo is resolved when the script is lowered\n", .{op.line});
        },
        else => {},
    }
}

/// The widest resolve() result: MAX_POINTS_PER_LINE points plus thickness and pointSize.
const scriptMaxPoints: usize = 200;

test "a line is appended as layout JSON the parser accepts" {
    const gpa = std.testing.allocator;
    var lines: std.ArrayList(u8) = .empty;
    defer lines.deinit(gpa);
    try appendLine(gpa, &lines, &.{ 1, 2, 3, 4 }, "#ccc", "dashed", "aqua", "red", 3, 2, true);
    try std.testing.expect(std.mem.indexOf(u8, lines.items, "\"locked\":true") != null);
    try std.testing.expect(std.mem.indexOf(u8, lines.items, "{\"x\":1,\"y\":2}") != null);

    const doc = try std.fmt.allocPrint(gpa, "{{\"imageWidth\":10,\"imageHeight\":10,\"lines\":[{s}]}}", .{lines.items});
    defer gpa.free(doc);
    var parsed = try layout_mod.parse(gpa, doc);
    defer parsed.deinit();
    try std.testing.expectEqual(@as(usize, 1), parsed.lines.len);
}

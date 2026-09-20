//! Synthetic inputs for the opt-in bench (`zig build bench`). Hermetic by design: no file
//! and no socket, so a bench run measures the code and never the machine's disk or network.
const std = @import("std");

/// A non-flat RGBA gradient so filters do real work and contour finds edges.
pub fn gradient(gpa: std.mem.Allocator, w: usize, h: usize) ![]u8 {
    const buf = try gpa.alloc(u8, w * h * 4);
    for (0..h) |y| {
        for (0..w) |x| {
            const i = (y * w + x) * 4;
            buf[i + 0] = @truncate(x);
            buf[i + 1] = @truncate(y);
            buf[i + 2] = @truncate(x ^ y);
            buf[i + 3] = 255;
        }
    }
    return buf;
}

/// A gallery page with `n` <img> tags plus a CSS background — the shape --source-site scans.
pub fn galleryHtml(gpa: std.mem.Allocator, n: usize) ![]u8 {
    var b: std.ArrayList(u8) = .empty;
    try b.appendSlice(gpa, "<html><head><base href=\"https://ex.test/g/\"><style>.h{background:url(bg.png)}</style></head><body>");
    for (0..n) |i| {
        var line: [160]u8 = undefined;
        try b.appendSlice(gpa, try std.fmt.bufPrint(&line, "<div class=h><img src=\"p{d}.jpg?v={d}\" alt=\"plate {d} &amp; more\"><source srcset=\"p{d}@2x.webp 2x\"></div>", .{ i, i, i, i }));
    }
    try b.appendSlice(gpa, "</body></html>");
    return b.toOwnedSlice(gpa);
}

/// A representative op-plan: reply, a mixed action list and two variants (contract §1–§3).
pub const plan_json =
    \\{"version":1,"reply":"Cropped, turned and toned.","actions":[{"op":"crop","spec":{"x1":"10%","x2":"-2cm","y1":"0","y2":"90px","aspect":"4:3"}},
    \\{"op":"rotate","dir":"left","times":2},{"op":"filter","mode":"custom","tint":"#A1b2c3"},{"op":"formula","axis":"y","expr":"y*2 + 1"},
    \\{"op":"page","format":"a4"},{"op":"layout","lines":[{"points":[{"x":1,"y":2},{"x":3,"y":4}],"color":"#FF0000","style":"dashed"}]},{"op":"save","name":"plate"}],
    \\"variants":[{"label":"Sepia","actions":[{"op":"filter","mode":"sepia"}]},{"label":"Inverted","actions":[{"op":"filter","mode":"invert"}]}]}
;

/// A valid single-`layout` plan whose one line carries `points` points — the size knob for
/// the validator's scaling ratio (every point is re-parsed and re-serialized).
pub fn layoutPlan(gpa: std.mem.Allocator, points: usize) ![]u8 {
    var b: std.ArrayList(u8) = .empty;
    try b.appendSlice(gpa, "{\"version\":1,\"reply\":\"Traced.\",\"actions\":[{\"op\":\"layout\",\"lines\":[{\"points\":[");
    for (0..points) |i| {
        var p: [48]u8 = undefined;
        if (i != 0) try b.append(gpa, ',');
        try b.appendSlice(gpa, try std.fmt.bufPrint(&p, "{{\"x\":{d},\"y\":{d}}}", .{ i % 900, (i * 2) % 900 }));
    }
    try b.appendSlice(gpa, "],\"color\":\"#FF0000\",\"style\":\"dashed\",\"thickness\":2}]}]}");
    return b.toOwnedSlice(gpa);
}

/// A `/prompt …` input line of `len` bytes — the editor themes the command word, then wraps.
pub fn promptLine(gpa: std.mem.Allocator, len: usize) ![]u8 {
    const buf = try gpa.alloc(u8, len);
    @memset(buf, 'x');
    @memcpy(buf[0..8], "/prompt ");
    return buf;
}

/// A `.stc` with `edits` shapes in one URL block — the shape `--script-emit` walks.
pub fn script(gpa: std.mem.Allocator, edits: usize) ![]u8 {
    var b: std.ArrayList(u8) = .empty;
    try b.appendSlice(gpa, "@source https://e.test/a.png:\n  @use line red dashed, 3px\n");
    for (0..edits) |i| {
        var line: [64]u8 = undefined;
        try b.appendSlice(gpa, try std.fmt.bufPrint(&line, "  @rect ({d}%, {d}%) (-{d}%, -{d}%)\n",
            .{ i % 40, (i * 3) % 40, i % 30, (i * 2) % 30 }));
    }
    try b.appendSlice(gpa, "  @save out/\n");
    return b.toOwnedSlice(gpa);
}

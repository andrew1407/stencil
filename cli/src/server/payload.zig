//! The layout envelope the CLI PUTs to a server project — the same shape
//! browser/js/core/layout.js builds. Serialized with std.json.Stringify, so escaping is the
//! stdlib's job and no field value can break out of its string.
const std = @import("std");
const testing = std.testing;

/// A crop rectangle in rotated-original pixels, for the layout envelope (kept core-free here).
pub const CropRect = struct { x: i64, y: i64, w: i64, h: i64 };

/// Page format + x/y formulas round-tripped through the layout (CLI preserves, doesn't edit).
/// Empty strings / zero dims are omitted from the JSON.
pub const PageMeta = struct {
    page_size: []const u8 = "", // "" | a named format ("A0".."C10") | "custom"
    custom_w: f64 = 0, // cm; 0 = unset
    custom_h: f64 = 0,
    allow_formulas: bool = false,
    formula_x: []const u8 = "", // "" = identity transform
    formula_y: []const u8 = "",
};

/// One request body, serialized from a struct. std.json owns the escaping, so a project name, keyword
/// or description holding a quote or a control byte can never break out of its field. Caller owns it.
pub fn body(gpa: std.mem.Allocator, value: anytype) ![]u8 {
    return std.json.Stringify.valueAlloc(gpa, value, .{});
}

pub fn buildLayout(
    gpa: std.mem.Allocator,
    w: i64,
    h: i64,
    lines_json: []const u8,
    filter_mode: []const u8,
    filter_color: []const u8,
    crop: ?CropRect,
    rotation: i32,
    meta: PageMeta,
) ![]u8 {
    var out: std.Io.Writer.Allocating = .init(gpa);
    errdefer out.deinit();
    var js: std.json.Stringify = .{ .writer = &out.writer };
    try js.beginObject();
    try js.objectField("imageWidth");
    try js.write(w);
    try js.objectField("imageHeight");
    try js.write(h);
    // The lines array arrives already serialized (the session keeps it as JSON text).
    try js.objectField("lines");
    try js.beginWriteRaw();
    try js.writer.writeAll(if (lines_json.len == 0) "[]" else lines_json);
    js.endWriteRaw();
    if (filter_mode.len != 0 and !std.ascii.eqlIgnoreCase(filter_mode, "none")) {
        try js.objectField("imageFilter");
        try js.write(filter_mode);
    }
    if (filter_color.len != 0) {
        try js.objectField("filterColor");
        try js.write(filter_color);
    }
    if (crop) |cr| {
        try js.objectField("cropRect");
        try js.beginObject();
        try js.objectField("x");
        try js.write(cr.x);
        try js.objectField("y");
        try js.write(cr.y);
        try js.objectField("width");
        try js.write(cr.w);
        try js.objectField("height");
        try js.write(cr.h);
        try js.endObject();
    }
    if (rotation != 0) {
        try js.objectField("rotationQuarters");
        try js.write(rotation);
    }
    if (meta.page_size.len != 0) {
        try js.objectField("pageSize");
        try js.write(meta.page_size);
    }
    // cm dims print as {d} (15, not 1.5e1) — the shape the GUI editors and the server read.
    if (meta.custom_w != 0) {
        try js.objectField("customPageWidth");
        try js.print("{d}", .{meta.custom_w});
    }
    if (meta.custom_h != 0) {
        try js.objectField("customPageHeight");
        try js.print("{d}", .{meta.custom_h});
    }
    if (meta.allow_formulas) {
        try js.objectField("allowFormulas");
        try js.write(true);
    }
    if (meta.formula_x.len != 0) {
        try js.objectField("formulaX");
        try js.write(meta.formula_x);
    }
    if (meta.formula_y.len != 0) {
        try js.objectField("formulaY");
        try js.write(meta.formula_y);
    }
    try js.endObject();
    return out.toOwnedSlice();
}

test "buildLayout emits optional fields only when set" {
    const a = testing.allocator;
    // Bare: just dims + empty lines (no filter/crop/rotation/page/formula).
    const bare = try buildLayout(a, 10, 20, "", "none", "", null, 0, .{});
    defer a.free(bare);
    try testing.expectEqualStrings("{\"imageWidth\":10,\"imageHeight\":20,\"lines\":[]}", bare);

    // Full: filter + color + crop + rotation, with a ready lines array passed through.
    const full = try buildLayout(a, 5, 6, "[{\"x\":1}]", "custom", "#7c3aed", .{ .x = 1, .y = 2, .w = 3, .h = 4 }, 3, .{});
    defer a.free(full);
    try testing.expect(std.mem.indexOf(u8, full, "\"lines\":[{\"x\":1}]") != null);
    try testing.expect(std.mem.indexOf(u8, full, "\"imageFilter\":\"custom\"") != null);
    try testing.expect(std.mem.indexOf(u8, full, "\"filterColor\":\"#7c3aed\"") != null);
    try testing.expect(std.mem.indexOf(u8, full, "\"cropRect\":{\"x\":1,\"y\":2,\"width\":3,\"height\":4}") != null);
    try testing.expect(std.mem.indexOf(u8, full, "\"rotationQuarters\":3") != null);
}

test "buildLayout round-trips page format + formulas (omit-when-default)" {
    const a = testing.allocator;
    // A custom page + x/y formulas survive into the envelope.
    const meta = PageMeta{
        .page_size = "custom",
        .custom_w = 15,
        .custom_h = 25,
        .allow_formulas = true,
        .formula_x = "x*2",
        .formula_y = "y+1",
    };
    const got = try buildLayout(a, 1, 1, "", "none", "", null, 0, meta);
    defer a.free(got);
    try testing.expect(std.mem.indexOf(u8, got, "\"pageSize\":\"custom\"") != null);
    try testing.expect(std.mem.indexOf(u8, got, "\"customPageWidth\":15") != null);
    try testing.expect(std.mem.indexOf(u8, got, "\"customPageHeight\":25") != null);
    try testing.expect(std.mem.indexOf(u8, got, "\"allowFormulas\":true") != null);
    try testing.expect(std.mem.indexOf(u8, got, "\"formulaX\":\"x*2\"") != null);
    try testing.expect(std.mem.indexOf(u8, got, "\"formulaY\":\"y+1\"") != null);

    // A named page with formulas off: pageSize kept, no formula keys, no allowFormulas.
    const named = try buildLayout(a, 1, 1, "", "none", "", null, 0, .{ .page_size = "A4" });
    defer a.free(named);
    try testing.expect(std.mem.indexOf(u8, named, "\"pageSize\":\"A4\"") != null);
    try testing.expect(std.mem.indexOf(u8, named, "allowFormulas") == null);
    try testing.expect(std.mem.indexOf(u8, named, "formulaX") == null);
}

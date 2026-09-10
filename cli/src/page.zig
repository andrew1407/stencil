//! Page-format policy: the A4 fallback, a blank's default pixel size, and the "<name>
//! <w>×<h>cm" label. Split out of pipeline.zig, which only ever hosted them — the
//! console (session header, /blank, /format) and the LLM plan executor are the other
//! callers. Pure: no image, no io.
const std = @import("std");
const core = @import("core.zig");

const BLANK_DPI = 96.0; // the screen DPI every blank is rasterized at (mirrors the browser)

/// A named page format's cm size, falling back to A4 — the one place that fallback lives.
pub fn pageSizeOrA4(gpa: std.mem.Allocator, name: []const u8) core.Page {
    return core.namedPageSize(gpa, name) orelse core.namedPageSize(gpa, "A4") orelse .{ .w = 21.0, .h = 29.7 };
}

/// A blank's default pixel size: explicit cm dims win, else the named page's (A4 when
/// unnamed/unknown), always at BLANK_DPI. The ONE blank-sizing derivation — the one-shot
/// --blank, the console's /blank and the LLM plan's blank op all come through here.
pub fn blankSizeFor(gpa: std.mem.Allocator, page: ?[]const u8, cm_w: f64, cm_h: f64) core.Size {
    const p = if (cm_w > 0 and cm_h > 0) core.Page{ .w = cm_w, .h = cm_h } else pageSizeOrA4(gpa, page orelse "A4");
    return core.defaultBlankSizePx(p.w, p.h, BLANK_DPI);
}

pub fn pageForImage(gpa: std.mem.Allocator, w: usize, h: usize) core.Page {
    return namedPageForImage(gpa, "A4", w, h);
}

/// A named page format's cm dims oriented to a `w`×`h` image (landscape swap, mirroring
/// core pageDimensions); an unknown name falls back to the A4 dims.
pub fn namedPageForImage(gpa: std.mem.Allocator, name: []const u8, w: usize, h: usize) core.Page {
    const base = pageSizeOrA4(gpa, name);
    // Landscape image -> lay the page on its side, mirroring core pageDimensions.
    if (w > h) return .{ .w = @max(base.w, base.h), .h = @min(base.w, base.h) };
    return .{ .w = @min(base.w, base.h), .h = @max(base.w, base.h) };
}

/// The page name the one-shot `wrote` line reports against: an applied layout's pageSize
/// wins, then --blank's picked format, else "" (→ the A4 default). Pure; unit-tested.
pub fn effectivePageName(layout_page: ?[]const u8, blank_page: ?[]const u8) []const u8 {
    return layout_page orelse (blank_page orelse "");
}

/// The page label printed next to the px size ("<name> <w>×<h>cm"): a named pick oriented
/// to the image, "custom <w>×<h>cm" for explicit cm dims, or the A4-derived default when
/// nothing is picked (empty name). The ONE derivation shared by the one-shot wrote line and
/// the console's header/save label (Session.pageFormatLabel). Caller owns the result.
pub fn pageLabelAlloc(gpa: std.mem.Allocator, page_size: []const u8, custom_w: f64, custom_h: f64, w: usize, h: usize) ![]u8 {
    var name: []const u8 = "A4";
    var dims = pageForImage(gpa, w, h);
    if (page_size.len != 0) {
        name = page_size;
        if (std.ascii.eqlIgnoreCase(page_size, "custom")) {
            // Custom dims are reported as picked (never orientation-swapped to the image).
            if (custom_w > 0 and custom_h > 0) dims = .{ .w = custom_w, .h = custom_h };
        } else {
            dims = namedPageForImage(gpa, page_size, w, h);
        }
    }
    return std.fmt.allocPrint(gpa, "{s} {d}×{d}cm", .{ name, dims.w, dims.h });
}

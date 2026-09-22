//! Page-format policy: the A4 fallback, a blank's default pixel size, and the "<name>
//! <w>×<h>cm" label. Split out of pipeline.zig, which only ever hosted them — the
//! console (session header, /blank, /format) and the LLM plan executor are the other
//! callers. Pure: no image, no io.
const std = @import("std");
const core = @import("../core.zig");

const BLANK_DPI = 96.0; // the screen DPI every blank is rasterized at (mirrors the browser)

/// A named page format's cm size, falling back to A4 — the one place that fallback lives.
pub fn pageSizeOrA4(name: []const u8) core.Page {
    const z = core.zstr(name) orelse "";
    return core.namedPageSize(z) orelse core.namedPageSize("A4") orelse .{ .w = 21.0, .h = 29.7 };
}

/// A blank's default pixel size: explicit cm dims win, else the named page's (A4 when unnamed), always
/// at BLANK_DPI. The ONE blank-sizing derivation — --blank, /blank and the LLM blank op share it.
pub fn blankSizeFor(page: ?[]const u8, cm_w: f64, cm_h: f64) core.Size {
    const p = if (cm_w > 0 and cm_h > 0) core.Page{ .w = cm_w, .h = cm_h } else pageSizeOrA4(page orelse "A4");
    return core.defaultBlankSizePx(p.w, p.h, BLANK_DPI);
}

pub fn pageForImage(w: usize, h: usize) core.Page {
    return namedPageForImage("A4", w, h);
}

/// A named page format's cm dims oriented to a `w`×`h` image (landscape swap, mirroring
/// core pageDimensions); an unknown name falls back to the A4 dims.
pub fn namedPageForImage(name: []const u8, w: usize, h: usize) core.Page {
    const base = pageSizeOrA4(name);
    // Landscape image -> lay the page on its side, mirroring core pageDimensions.
    if (w > h) return .{ .w = @max(base.w, base.h), .h = @min(base.w, base.h) };
    return .{ .w = @min(base.w, base.h), .h = @max(base.w, base.h) };
}

/// The page name the one-shot `wrote` line reports against: an applied layout's pageSize
/// wins, then --blank's picked format, else "" (→ the A4 default). Pure; unit-tested.
pub fn effectivePageName(layout_page: ?[]const u8, blank_page: ?[]const u8) []const u8 {
    return layout_page orelse (blank_page orelse "");
}

/// A session's page in cm: custom dims as picked (never orientation-swapped), a named format
/// oriented to the image, else the A4 default. The ONE derivation the `wrote` label and the
/// formula context share.
pub fn pageCmFor(page_size: []const u8, custom_w: f64, custom_h: f64, w: usize, h: usize) core.Page {
    if (page_size.len == 0) return pageForImage(w, h);
    if (std.ascii.eqlIgnoreCase(page_size, "custom")) {
        if (custom_w > 0 and custom_h > 0) return .{ .w = custom_w, .h = custom_h };
        return pageForImage(w, h);
    }
    return namedPageForImage(page_size, w, h);
}

/// The page label beside the px size ("<name> <w>×<h>cm", "custom <w>×<h>cm", or the A4 default). The
/// ONE derivation shared by the one-shot wrote line and Session.pageFormatLabel; caller owns it.
pub fn pageLabelAlloc(gpa: std.mem.Allocator, page_size: []const u8, custom_w: f64, custom_h: f64, w: usize, h: usize) ![]u8 {
    const name: []const u8 = if (page_size.len != 0) page_size else "A4";
    const dims = pageCmFor(page_size, custom_w, custom_h, w, h);
    return std.fmt.allocPrint(gpa, "{s} {d}×{d}cm", .{ name, dims.w, dims.h });
}

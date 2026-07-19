// WebAssembly API surface for the shared Stencil core (S15).
//
// Thin extern "C" wrappers over the GUI-free core logic so the browser app can
// replace its JS engines (formulaEngine.js, the geometry parts of utils.js, and
// the page-calc parts of drawingApp.js) with this compiled core. STL-only — no
// Qt, never linked into the desktop binary (see CMakeLists EMSCRIPTEN branch).
// See desktop/WASM.md for the build + browser-wiring instructions.
//
// extern "C" (not embind) was chosen to keep the surface minimal and ABI-stable:
// every export is a plain C function over doubles / C strings, which Emscripten
// exposes via Module.ccall / cwrap with no extra runtime.

#include "color.hpp"
#include "cropGeometry.hpp"
#include "durationParser.hpp"
#include "formulaParser.hpp"
#include "geometry.hpp"
#include "imageFilter.hpp"
#include "pageMetrics.hpp"
#include "zoomPan.hpp"
#include <cstdint>
#include <vector>

using namespace stencil::core;

namespace {
  // Marshal a flat [x0,y0,x1,y1,...] array of `count` points into a vector.
  std::vector<Point> toPoints(const double* pts, int count) {
    std::vector<Point> v;
    if (count > 0) v.reserve(count);
    for (int i = 0; i < count; ++i) v.push_back(Point{pts[2 * i], pts[2 * i + 1]});
    return v;
  }

  // Write a CropRect to out[0..3] = {x, y, width, height}.
  void writeRect(const CropRect& r, double* out) {
    out[0] = r.x;
    out[1] = r.y;
    out[2] = r.width;
    out[3] = r.height;
  }
}

extern "C" {

  // ── color (utils.js parseHex / hexToRgba) ──
  // Parse "#rrggbb" -> out[0..2] = {r, g, b}. Returns 1 on success, 0 if the
  // string is not a 7-char hex (out is left untouched). The browser builds the
  // "rgba(...)" string itself from these components, matching utils.js hexToRgba.
  int stencil_parseHex(const char* hex, int* out) {
    const auto rgb = parseHex(hex ? hex : "");
    if (!rgb.has_value()) return 0;
    out[0] = rgb->r;
    out[1] = rgb->g;
    out[2] = rgb->b;
    return 1;
  }

  // ── geometry (utils.js distToSegment) ──
  double stencil_distToSegment(double px, double py, double ax, double ay,
                               double bx, double by) {
    return distToSegment(px, py, Point{ax, ay}, Point{bx, by});
  }

  // ── drawing gate (drawingApp.js #closeCurrentShape) ──
  // Returns 1 if a click at (cx,cy) closes a shape built from a flat [x0,y0,...]
  // array of `count` points with the given markerSize, else 0.
  int stencil_shouldCloseShape(const double* pts, int count, double cx,
                               double cy, double markerSize) {
    const std::vector<Point> v = toPoints(pts, count);
    return shouldCloseShape(v, Point{cx, cy}, markerSize) ? 1 : 0;
  }

  // ── page metrics (drawingApp.js getPageDimensions / pixelToPageCoords) ──
  // `name` is any canonical ISO format name from stencil_pageFormats ("A0".."C10")
  // or "custom"; custom* used only when name=="custom". Results are written to
  // outW/outH (page cm) for pageDimensions, and outX/outY (page cm, raw) for
  // pixelToPageRaw.
  void stencil_pageDimensions(const char* name, int canvasW, int canvasH,
                              double customW, double customH, double* outW,
                              double* outH) {
    const PageSize ps = pageDimensions(name ? name : "", canvasW, canvasH,
                                       customW, customH);
    *outW = ps.width;
    *outH = ps.height;
  }

  void stencil_pixelToPageRaw(double x, double y, double dimW, double dimH,
                              int canvasW, int canvasH, double* outX,
                              double* outY) {
    const Point p = pixelToPageRaw(x, y, PageSize{dimW, dimH}, canvasW, canvasH);
    *outX = p.x;
    *outY = p.y;
  }

  // The canonical page-format names ("A0 A1 … C10", space-separated, no
  // "custom") in the canonical A/B/C-series order. Static storage — the
  // browser reads it as a string, never frees it.
  const char* stencil_pageFormats(void) { return pageFormatNames(); }

  // ── formula engine (formulaEngine.js validate / apply / evaluate) ──
  // varName is the ASCII code of 'x' or 'y'.
  int stencil_formulaValidate(const char* expr, int varName) {
    return FormulaParser::validate(expr ? expr : "", static_cast<char>(varName)) ? 1 : 0;
  }

  double stencil_formulaApply(const char* expr, int varName, double value,
                              int allowFormulas) {
    return FormulaParser::apply(expr ? expr : "", static_cast<char>(varName), value,
                                allowFormulas != 0);
  }

  // Returns 1 and writes the result to *out on success; returns 0 on a parse
  // error or non-finite result (leaving *out untouched).
  int stencil_formulaEvaluate(const char* expr, int varName, double varValue,
                              double* out) {
    const auto r =
        FormulaParser::evaluate(expr ? expr : "", static_cast<char>(varName), varValue);
    if (!r.has_value()) return 0;
    *out = *r;
    return 1;
  }

  // ── duration parser (durationParser.js parse) ──
  // Parse a human duration ("days 23", "fortnight", "off") into milliseconds
  // written to *out (0 for off/never). Returns 1 on a valid spec, 0 otherwise
  // (leaving *out untouched). The browser adds *out to Date.now() for the expiry.
  int stencil_parseDuration(const char* spec, double* out) {
    static const DurationParser dp;
    long long ms = 0;
    if (!dp.parse(spec ? spec : "", ms)) return 0;
    *out = static_cast<double>(ms);
    return 1;
  }

  // ── image filters (renderer.js drawImageWithFilter / #applyTintFilter) ──
  // Apply a filter in place to an interleaved RGBA8 buffer of `pixelCount`
  // pixels (a canvas ImageData.data layout). `mode`: 0 none, 1 bw, 2 sepia,
  // 3 custom, 4 invert, 5 contour (a no-op here — contour needs dimensions,
  // use stencil_applyContourRGBA); tint* are used only for the custom duotone.
  // Alpha is preserved.
  void stencil_applyFilterRGBA(int mode, std::uint8_t* data, int pixelCount,
                               int tintR, int tintG, int tintB) {
    applyFilterRGBA(static_cast<FilterMode>(mode), data,
                    pixelCount < 0 ? 0 : static_cast<std::size_t>(pixelCount),
                    tintR, tintG, tintB);
  }

  // Sobel edge detection ("contour") in place on a w x h RGBA8 buffer: dark
  // edges on a white page, alpha preserved. Pinned integer math — the JS
  // fallback (contourFilter.js) must stay byte-identical. Degenerate sizes /
  // null data are a no-op.
  void stencil_applyContourRGBA(std::uint8_t* data, int w, int h) {
    applyContourRGBA(data, w, h);
  }

  // ── geometry transforms (drawingApp.js #rotateSelectedLine) ──
  // Rotate a flat [x0,y0,x1,y1,...] array of `count` points in place about
  // (cx,cy) by `angle` radians.
  void stencil_rotatePoints(double* pts, int count, double cx, double cy,
                            double angle) {
    if (!pts || count <= 0) return;
    std::vector<Point> v = toPoints(pts, count);
    rotatePoints(v, cx, cy, angle);
    for (int i = 0; i < count; ++i) {
      pts[2 * i] = v[i].x;
      pts[2 * i + 1] = v[i].y;
    }
  }

  // ── geometry transforms (drawingApp.js #flipSelectedLine) ──
  // Mirror a flat [x0,y0,x1,y1,...] array of `count` points in place about
  // (cx,cy): horizontal != 0 reflects x, else reflects y.
  void stencil_flipPoints(double* pts, int count, int horizontal, double cx,
                          double cy) {
    if (!pts || count <= 0) return;
    std::vector<Point> v = toPoints(pts, count);
    flipPoints(v, horizontal != 0, cx, cy);
    for (int i = 0; i < count; ++i) {
      pts[2 * i] = v[i].x;
      pts[2 * i + 1] = v[i].y;
    }
  }

  // Center of the axis-aligned bounding box of a flat point array -> out[0..1].
  // The rotation pivot used by #rotateSelectedLine when no point is focused.
  void stencil_boundingBoxCenter(const double* pts, int count, double* out) {
    const std::vector<Point> v = toPoints(pts, count);
    const Point c = boundingBoxCenter(v);
    out[0] = c.x;
    out[1] = c.y;
  }

  // ── zoom/pan math (zoomPan.js) ──
  double stencil_clampScale(double scale) { return clampScale(scale); }

  // Anchored (toward-cursor) zoom -> out[0..2] = {scale, scrollLeft, scrollTop}.
  void stencil_anchoredZoom(double scrollLeft, double scrollTop, double cursorX,
                            double cursorY, double oldScale, double newScale,
                            double* out) {
    const AnchoredZoom z = anchoredZoom(scrollLeft, scrollTop, cursorX, cursorY,
                                        oldScale, newScale);
    out[0] = z.scale;
    out[1] = z.scrollLeft;
    out[2] = z.scrollTop;
  }

  // Zoom-to-rect -> out[0..2] = {scale, scrollLeft, scrollTop}.
  void stencil_rectZoom(double x1, double y1, double rectW, double rectH,
                        double availW, double availH, double* out) {
    const RectZoom z = rectZoom(x1, y1, rectW, rectH, availW, availH);
    out[0] = z.scale;
    out[1] = z.scrollLeft;
    out[2] = z.scrollTop;
  }

  // ── crop geometry (cropGeometry.js; shared with the Qt crop dialog) ──
  // Each CropRect result is written to out[0..3] = {x, y, width, height}.
  int stencil_isAlbumOrientation(double width, double height) {
    return isAlbumOrientation(width, height) ? 1 : 0;
  }

  double stencil_cropAspect(double pageWidth, double pageHeight, int album) {
    return cropAspect(pageWidth, pageHeight, album != 0);
  }

  void stencil_centeredCrop(double imageW, double imageH, double aspectWoverH,
                            double* out) {
    const CropRect r = centeredCrop(imageW, imageH, aspectWoverH);
    writeRect(r, out);
  }

  void stencil_resizeCropFromCorner(double x, double y, double w, double h,
                                    int corner, double cursorX, double cursorY,
                                    double aspectWoverH, double imageW,
                                    double imageH, double minSize, double* out) {
    const CropRect r = resizeCropFromCorner(CropRect{x, y, w, h}, corner, cursorX,
                                            cursorY, aspectWoverH, imageW, imageH,
                                            minSize);
    writeRect(r, out);
  }

  void stencil_moveCropClamped(double x, double y, double w, double h, double dx,
                               double dy, double imageW, double imageH,
                               double* out) {
    const CropRect r = moveCropClamped(CropRect{x, y, w, h}, dx, dy, imageW, imageH);
    writeRect(r, out);
  }

  void stencil_scaleCropCentered(double x, double y, double w, double h, double factor,
                                 double aspectWoverH, double imageW, double imageH,
                                 double* out) {
    const CropRect r = scaleCropCentered(CropRect{x, y, w, h}, factor, aspectWoverH,
                                         imageW, imageH);
    writeRect(r, out);
  }

  double stencil_cropResizeScale(double oldWidth, double newWidth) {
    return cropResizeScale(oldWidth, newWidth);
  }

  // Rotate a crop rect one quarter turn (clockwise != 0 = right) within an image
  // of imageW x imageH -> out[0..3] = {x, y, width, height}. Crop-local line
  // points ride along via the plain rotateLinePointsQuarter core call (the
  // browser runs that one in JS, like scaleLinePoints), so it needs no export.
  void stencil_rotateCropRectQuarter(double x, double y, double w, double h,
                                     double imageW, double imageH, int clockwise,
                                     double* out) {
    const CropRect r = rotateCropRectQuarter(CropRect{x, y, w, h}, imageW, imageH,
                                             clockwise != 0);
    writeRect(r, out);
  }

  // out[0] = orientationChanged (0/1), out[1] = scale.
  void stencil_cropChange(double oldX, double oldY, double oldW, double oldH,
                          double newX, double newY, double newW, double newH,
                          double* out) {
    const CropChange c = cropChange(CropRect{oldX, oldY, oldW, oldH},
                                    CropRect{newX, newY, newW, newH});
    out[0] = c.orientationChanged ? 1.0 : 0.0;
    out[1] = c.scale;
  }

}  // extern "C"

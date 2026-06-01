// WebAssembly API surface for the shared Stencil core (S15).
//
// Thin extern "C" wrappers over the GUI-free core logic so the browser app can
// replace its JS engines (formulaEngine.js, the geometry parts of utils.js, and
// the page-calc parts of drawingApp.js) with this compiled core. STL-only — no
// Qt, never linked into the desktop binary (see CMakeLists EMSCRIPTEN branch).
//
// emcc is NOT installed in this environment, so this translation unit is
// build-ready but was not compiled here. See desktop/WASM.md for the build +
// browser-wiring instructions.
//
// extern "C" (not embind) was chosen to keep the surface minimal and ABI-stable:
// every export is a plain C function over doubles / C strings, which Emscripten
// exposes via Module.ccall / cwrap with no extra runtime.

#include "formulaParser.hpp"
#include "geometry.hpp"
#include "imageFilter.hpp"
#include "pageMetrics.hpp"
#include "zoomPan.hpp"
#include <cstdint>
#include <vector>

using namespace stencil::core;

extern "C" {

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
    std::vector<Point> v;
    v.reserve(count);
    for (int i = 0; i < count; ++i) v.push_back(Point{pts[2 * i], pts[2 * i + 1]});
    return shouldCloseShape(v, Point{cx, cy}, markerSize) ? 1 : 0;
  }

  // ── page metrics (drawingApp.js getPageDimensions / pixelToPageCoords) ──
  // `name` is "A3" | "A4" | "custom"; custom* used only when name=="custom".
  // Results are written to outW/outH (page cm) for pageDimensions, and
  // outX/outY (page cm, raw) for pixelToPageRaw.
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

  // ── formula engine (formulaEngine.js validate / apply / evaluate) ──
  // varName is the ASCII code of 'x' or 'y'.
  int stencil_formulaValidate(const char* expr, int varName) {
    static const FormulaParser fp;
    return fp.validate(expr ? expr : "", static_cast<char>(varName)) ? 1 : 0;
  }

  double stencil_formulaApply(const char* expr, int varName, double value,
                              int allowFormulas) {
    static const FormulaParser fp;
    return fp.apply(expr ? expr : "", static_cast<char>(varName), value,
                    allowFormulas != 0);
  }

  // Returns 1 and writes the result to *out on success; returns 0 on a parse
  // error or non-finite result (leaving *out untouched).
  int stencil_formulaEvaluate(const char* expr, int varName, double varValue,
                              double* out) {
    static const FormulaParser fp;
    const auto r =
        fp.evaluate(expr ? expr : "", static_cast<char>(varName), varValue);
    if (!r.has_value()) return 0;
    *out = *r;
    return 1;
  }

  // ── image filters (renderer.js drawImageWithFilter / #applyTintFilter) ──
  // Apply a filter in place to an interleaved RGBA8 buffer of `pixelCount`
  // pixels (a canvas ImageData.data layout). `mode`: 0 none, 1 bw, 2 sepia,
  // 3 custom; tint* are used only for the custom duotone. Alpha is preserved.
  void stencil_applyFilterRGBA(int mode, std::uint8_t* data, int pixelCount,
                               int tintR, int tintG, int tintB) {
    applyFilterRGBA(static_cast<FilterMode>(mode), data,
                    pixelCount < 0 ? 0 : static_cast<std::size_t>(pixelCount),
                    tintR, tintG, tintB);
  }

  // ── geometry transforms (drawingApp.js #rotateSelectedLine) ──
  // Rotate a flat [x0,y0,x1,y1,...] array of `count` points in place about
  // (cx,cy) by `angle` radians.
  void stencil_rotatePoints(double* pts, int count, double cx, double cy,
                            double angle) {
    if (!pts || count <= 0) return;
    std::vector<Point> v;
    v.reserve(count);
    for (int i = 0; i < count; ++i) v.push_back(Point{pts[2 * i], pts[2 * i + 1]});
    rotatePoints(v, cx, cy, angle);
    for (int i = 0; i < count; ++i) {
      pts[2 * i] = v[i].x;
      pts[2 * i + 1] = v[i].y;
    }
  }

  // Center of the axis-aligned bounding box of a flat point array -> out[0..1].
  // The rotation pivot used by #rotateSelectedLine when no point is focused.
  void stencil_boundingBoxCenter(const double* pts, int count, double* out) {
    std::vector<Point> v;
    v.reserve(count > 0 ? count : 0);
    for (int i = 0; i < count; ++i) v.push_back(Point{pts[2 * i], pts[2 * i + 1]});
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

}  // extern "C"

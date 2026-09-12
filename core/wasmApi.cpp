// WebAssembly API surface for the shared Stencil core.
//
// Thin extern "C" wrappers over the GUI-free core, so the browser runs the
// compiled core in place of its JS fallbacks. STL-only, never linked into the
// desktop binary (CMakeLists EMSCRIPTEN branch). Wiring: core/WASM.md.
//
// extern "C" (not embind) was chosen to keep the surface minimal and ABI-stable:
// every export is a plain C function over doubles / C strings, which Emscripten
// exposes via Module.ccall / cwrap with no extra runtime.

#include "marshal.hpp"

#include "color.hpp"
#include "durationParser.hpp"
#include "formulaParser.hpp"
#include "hitTest.hpp"
#include "imageFilter.hpp"
#include "pageMetrics.hpp"
#include "pointMath.hpp"
#include "zoomPan.hpp"
#include <cstdint>
#include <vector>

using namespace stencil::core;

namespace {
  using abi::toPoints;
}

extern "C" {

  // ── color (utils/color.js parseHex / hexToRgba) ──
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

  // ── geometry (utils/geometry.js distToSegment) ──
  double stencil_distToSegment(double px, double py, double ax, double ay,
                               double bx, double by) {
    return distToSegment(px, py, Point{ax, ay}, Point{bx, by});
  }

  // ── drawing gate (lineTransforms.js shouldCloseShape) ──
  // Returns 1 if a click at (cx,cy) closes a shape built from a flat [x0,y0,...]
  // array of `count` points with the given pointSize, else 0.
  int stencil_shouldCloseShape(const double* pts, int count, double cx,
                               double cy, double pointSize) {
    const std::vector<Point> v = toPoints(pts, count);
    return shouldCloseShape(v, Point{cx, cy}, pointSize) ? 1 : 0;
  }

  // ── page metrics (pageMetrics.js getPageDimensions / pixelToPageCoords) ──
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

  // ── formula engine (formulaEngine.js validate / apply / evaluate) ──
  // varName is the ASCII code of 'x' or 'y'.
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

  // ── geometry transforms (lineTransforms.js rotatePointsAbout) ──
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

  // ── geometry transforms (lineTransforms.js flipPointsAbout) ──
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
  // The rotation pivot lineTransforms.js uses when no point is focused.
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

  // Five more exports are emitted here from abi/shared.inc, shared verbatim
  // with the other ABI.
#define STENCIL_ABI(wasmName, cliName) stencil_##wasmName
#include "shared.inc"
#undef STENCIL_ABI

}  // extern "C"

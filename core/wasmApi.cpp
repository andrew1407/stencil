// WebAssembly ABI over the core: plain extern "C" functions over doubles / C strings
// (ccall/cwrap, no embind runtime). Never linked into the desktop. Wiring: core/WASM.md.

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

  // utils/color.js parseHex: "#rrggbb" -> out[0..2] = {r, g, b}; 0 (out untouched)
  // unless a 7-char hex. The browser builds the "rgba(...)" string itself.
  int stencil_parseHex(const char* hex, int* out) {
    const auto rgb = parseHex(hex ? hex : "");
    if (!rgb.has_value()) return 0;
    out[0] = rgb->r;
    out[1] = rgb->g;
    out[2] = rgb->b;
    return 1;
  }

  double stencil_distToSegment(double px, double py, double ax, double ay,
                               double bx, double by) {
    return distToSegment(px, py, Point{ax, ay}, Point{bx, by});
  }

  // Point arrays cross as flat [x0,y0,x1,y1,…] of `count` pairs (abi/marshal.hpp).
  int stencil_shouldCloseShape(const double* pts, int count, double cx,
                               double cy, double pointSize) {
    const std::vector<Point> v = toPoints(pts, count);
    return shouldCloseShape(v, Point{cx, cy}, pointSize) ? 1 : 0;
  }

  // `name` is a stencil_pageFormats name or "custom" (custom* read only then); page cm out.
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

  // varName is the ASCII code of 'x' or 'y'; 0 (out untouched) on a parse error or
  // non-finite result.
  int stencil_formulaEvaluate(const char* expr, int varName, double varValue,
                              double* out) {
    const auto r =
        FormulaParser::evaluate(expr ? expr : "", static_cast<char>(varName), varValue);
    if (!r.has_value()) return 0;
    *out = *r;
    return 1;
  }

  // `mode` is the FilterMode code (0 none … 5 contour, a no-op here — it needs
  // dimensions, see stencil_applyContourRGBA); `data` is a canvas ImageData.data.
  void stencil_applyFilterRGBA(int mode, std::uint8_t* data, int pixelCount,
                               int tintR, int tintG, int tintB) {
    applyFilterRGBA(static_cast<FilterMode>(mode), data,
                    pixelCount < 0 ? 0 : static_cast<std::size_t>(pixelCount),
                    tintR, tintG, tintB);
  }

  // In place; `angle` in radians.
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

  // In place; horizontal != 0 reflects x, else y.
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

  // -> out[0..1].
  void stencil_boundingBoxCenter(const double* pts, int count, double* out) {
    const std::vector<Point> v = toPoints(pts, count);
    const Point c = boundingBoxCenter(v);
    out[0] = c.x;
    out[1] = c.y;
  }

  double stencil_clampScale(double scale) { return clampScale(scale); }

  // Both zooms -> out[0..2] = {scale, scrollLeft, scrollTop}.
  void stencil_anchoredZoom(double scrollLeft, double scrollTop, double cursorX,
                            double cursorY, double oldScale, double newScale,
                            double* out) {
    const AnchoredZoom z = anchoredZoom(scrollLeft, scrollTop, cursorX, cursorY,
                                        oldScale, newScale);
    out[0] = z.scale;
    out[1] = z.scrollLeft;
    out[2] = z.scrollTop;
  }

  void stencil_rectZoom(double x1, double y1, double rectW, double rectH,
                        double availW, double availH, double* out) {
    const RectZoom z = rectZoom(x1, y1, rectW, rectH, availW, availH);
    out[0] = z.scale;
    out[1] = z.scrollLeft;
    out[2] = z.scrollTop;
  }

  // Five more exports come from abi/shared.inc, verbatim with the CLI ABI.
#define STENCIL_ABI(wasmName, cliName) stencil_##wasmName
#include "shared.inc"
#undef STENCIL_ABI

}  // extern "C"

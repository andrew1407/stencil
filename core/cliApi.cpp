#include "cliApi.h"

#include "colorNames.hpp"
#include "cropSpec.hpp"
#include "formulaParser.hpp"
#include "imageFilter.hpp"
#include "imageOps.hpp"
#include "pageMetrics.hpp"
#include "rasterize.hpp"

#include <algorithm>
#include <cmath>
#include <string>

using namespace stencil::core;

extern "C" {

  int stencil_cli_parseColor(const char* spec, int* r, int* g, int* b, int* a) {
    const auto c = parseColor(spec ? std::string(spec) : std::string());
    if (!c) return 0;
    if (r) *r = c->r;
    if (g) *g = c->g;
    if (b) *b = c->b;
    if (a) *a = c->a;
    return 1;
  }

  int stencil_cli_namedPageSize(const char* name, double* wcm, double* hcm) {
    const PageSize ps = namedPageSize(name ? std::string(name) : std::string());
    if (ps.width <= 0.0 || ps.height <= 0.0) return 0;
    if (wcm) *wcm = ps.width;
    if (hcm) *hcm = ps.height;
    return 1;
  }

  const char* stencil_cli_pageFormats(void) { return pageFormatNames(); }

  void stencil_cli_defaultBlankSizePx(double pageWcm, double pageHcm, double dpi,
                                      int* outW, int* outH) {
    const SizePx s = defaultBlankSizePx(PageSize{pageWcm, pageHcm}, dpi);
    if (outW) *outW = s.width;
    if (outH) *outH = s.height;
  }

  int stencil_cli_resolveCrop(const char* spec, double imageW, double imageH,
                              double pxPerCmX, double pxPerCmY,
                              double pageWcm, double pageHcm, int album,
                              int* outX, int* outY, int* outW, int* outH) {
    const CropSpec cs = parseCropSpec(spec ? std::string(spec) : std::string());
    if (!cs.valid) return 0;

    CropResolveParams p;
    p.imageW = imageW;
    p.imageH = imageH;
    p.pxPerCmX = pxPerCmX;
    p.pxPerCmY = pxPerCmY;
    p.pageWidth = pageWcm;
    p.pageHeight = pageHcm;

    const auto rect = resolveCropRect(cs, p, album != 0);
    if (!rect) return 0;

    const int iw = static_cast<int>(std::lround(imageW));
    const int ih = static_cast<int>(std::lround(imageH));
    int x = std::clamp(static_cast<int>(std::lround(rect->x)), 0, std::max(0, iw));
    int y = std::clamp(static_cast<int>(std::lround(rect->y)), 0, std::max(0, ih));
    int w = std::clamp(static_cast<int>(std::lround(rect->width)), 0, iw - x);
    int h = std::clamp(static_cast<int>(std::lround(rect->height)), 0, ih - y);
    if (w <= 0 || h <= 0) return 0;  // an empty crop is not useful

    if (outX) *outX = x;
    if (outY) *outY = y;
    if (outW) *outW = w;
    if (outH) *outH = h;
    return 1;
  }

  void stencil_cli_cropImageRGBA(const uint8_t* src, int srcW, int srcH,
                                 int rx, int ry, int rw, int rh, uint8_t* dst) {
    cropImageRGBA(src, srcW, srcH, rx, ry, rw, rh, dst);
  }

  int stencil_cli_normalizeQuarters(int quarters) {
    return normalizeQuarters(quarters);
  }

  void stencil_cli_rotatedDims(int w, int h, int quarters, int* outW, int* outH) {
    int ow = w, oh = h;
    rotatedDims(w, h, quarters, ow, oh);
    if (outW) *outW = ow;
    if (outH) *outH = oh;
  }

  void stencil_cli_rotateImageRGBA(const uint8_t* src, int w, int h, int quarters,
                                   uint8_t* dst) {
    rotateImageRGBA(src, w, h, quarters, dst);
  }

  void stencil_cli_fillRGBA(uint8_t* dst, int pixelCount, int r, int g, int b, int a) {
    if (pixelCount < 0) return;
    fillRGBA(dst, static_cast<std::size_t>(pixelCount), r, g, b, a);
  }

  void stencil_cli_applyFilter(const char* mode, uint8_t* data, int pixelCount,
                               int tintR, int tintG, int tintB) {
    if (pixelCount <= 0) return;
    const FilterMode fm = filterModeFromString(mode ? std::string(mode) : std::string());
    applyFilterRGBA(fm, data, static_cast<std::size_t>(pixelCount), tintR, tintG, tintB);
  }

  void stencil_cli_applyContour(uint8_t* data, int width, int height) {
    applyContourRGBA(data, width, height);
  }

  void stencil_cli_rasterizeLine(uint8_t* buf, int w, int h,
                                 const double* pts, int nPts,
                                 const char* color, double thickness, double markerSize,
                                 const char* style, int locked, const char* fillColor) {
    if (nPts <= 0 || pts == nullptr) return;
    Line line;
    line.points.reserve(static_cast<std::size_t>(nPts));
    for (int i = 0; i < nPts; ++i)
      line.points.push_back(Point{pts[i * 2], pts[i * 2 + 1]});
    if (color) line.color = color;
    line.thickness = thickness;
    line.markerSize = markerSize;
    if (style) line.style = style;
    line.locked = locked != 0;
    if (fillColor) line.fillColor = fillColor;
    rasterizeLine(buf, w, h, line);
  }

  int stencil_cli_validateFormula(const char* expr, int var) {
    static const FormulaParser fp;
    return fp.validate(std::string(expr ? expr : ""), static_cast<char>(var)) ? 1 : 0;
  }

  double stencil_cli_applyFormula(const char* expr, int var, double value,
                                  int allowFormulas) {
    static const FormulaParser fp;
    return fp.apply(std::string(expr ? expr : ""), static_cast<char>(var), value,
                    allowFormulas != 0);
  }

}  // extern "C"

#include "pageMetrics.hpp"

namespace stencil::core {

  // Mirrors PAGE_SIZES in browser/js/config/constants.json.
  PageSize namedPageSize(const std::string& name) {
    if (name == "A3") return {29.7, 42.0};
    if (name == "A4") return {21.0, 29.7};
    return {0.0, 0.0};
  }

  // Port of DrawingApp.getPageDimensions: custom passes through; a named size
  // swaps to landscape when the image is wider than tall.
  PageSize pageDimensions(const std::string& name,
                          int canvasWidth, int canvasHeight,
                          double customWidth, double customHeight) {
    if (name == "custom") return {customWidth, customHeight};
    const PageSize ps = namedPageSize(name);
    if (canvasWidth > canvasHeight) return {ps.height, ps.width};
    return ps;
  }

  // Mirrors defaultBlankSizePx in browser/js/core/layout.js: cm / 2.54 * dpi,
  // rounded, never below 1px.
  SizePx defaultBlankSizePx(const PageSize& page, double dpi) {
    const auto toPx = [dpi](double cm) {
      const int px = static_cast<int>(cm / 2.54 * dpi + 0.5);
      return px < 1 ? 1 : px;
    };
    return {toPx(page.width), toPx(page.height)};
  }

  // Port of DrawingApp.pixelToPageCoords (raw part, pre-formula).
  Point pixelToPageRaw(double x, double y,
                       const PageSize& dims, int canvasWidth, int canvasHeight) {
    Point p;
    p.x = (canvasWidth  != 0) ? (dims.width  / canvasWidth)  * x : 0.0;
    p.y = (canvasHeight != 0) ? (dims.height / canvasHeight) * y : 0.0;
    return p;
  }

}

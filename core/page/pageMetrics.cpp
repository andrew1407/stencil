#include "pageMetrics.hpp"

namespace stencil::core {

  namespace {
    // The full ISO 216 A/B series + ISO 269 C series (portrait cm — the ISO mm
    // values / 10), in the canonical A0..A10, B0..B10, C0..C10 order used by
    // every list/selector. Mirrors PAGE_SIZES in browser/js/config/constants.json.
    struct NamedSize {
      const char* name;
      PageSize size;
    };
    constexpr NamedSize kPageSizes[] = {
        {"A0", {84.1, 118.9}}, {"A1", {59.4, 84.1}}, {"A2", {42.0, 59.4}},
        {"A3", {29.7, 42.0}},  {"A4", {21.0, 29.7}}, {"A5", {14.8, 21.0}},
        {"A6", {10.5, 14.8}},  {"A7", {7.4, 10.5}},  {"A8", {5.2, 7.4}},
        {"A9", {3.7, 5.2}},    {"A10", {2.6, 3.7}},
        {"B0", {100.0, 141.4}}, {"B1", {70.7, 100.0}}, {"B2", {50.0, 70.7}},
        {"B3", {35.3, 50.0}},   {"B4", {25.0, 35.3}},  {"B5", {17.6, 25.0}},
        {"B6", {12.5, 17.6}},   {"B7", {8.8, 12.5}},   {"B8", {6.2, 8.8}},
        {"B9", {4.4, 6.2}},     {"B10", {3.1, 4.4}},
        {"C0", {91.7, 129.7}}, {"C1", {64.8, 91.7}}, {"C2", {45.8, 64.8}},
        {"C3", {32.4, 45.8}},  {"C4", {22.9, 32.4}}, {"C5", {16.2, 22.9}},
        {"C6", {11.4, 16.2}},  {"C7", {8.1, 11.4}},  {"C8", {5.7, 8.1}},
        {"C9", {4.0, 5.7}},    {"C10", {2.8, 4.0}},
    };
  }

  PageSize namedPageSize(const std::string& name) {
    for (const NamedSize& ns : kPageSizes)
      if (name == ns.name) return ns.size;
    return {0.0, 0.0};
  }

  const char* pageFormatNames() {
    static const std::string names = [] {
      std::string s;
      for (const NamedSize& ns : kPageSizes) {
        if (!s.empty()) s += ' ';
        s += ns.name;
      }
      return s;
    }();
    return names.c_str();
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

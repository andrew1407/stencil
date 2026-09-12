#include "pageMetrics.hpp"

namespace stencil::core {

  namespace {
    // ISO 216 A/B + ISO 269 C, portrait cm, in the order every selector uses. Mirrors
    // PAGE_SIZES in browser/js/config/constants.json (drift-tested by cli/tests).
    struct NamedSize {
      const char* name;
      PageSize size;
    };
    constexpr NamedSize PAGE_SIZES[] = {
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
    for (const NamedSize& ns : PAGE_SIZES)
      if (name == ns.name) return ns.size;
    return {0.0, 0.0};
  }

  const char* pageFormatNames() {
    static const std::string names = [] {
      std::string s;
      for (const NamedSize& ns : PAGE_SIZES) {
        if (!s.empty()) s += ' ';
        s += ns.name;
      }
      return s;
    }();
    return names.c_str();
  }

  PageSize pageDimensions(const std::string& name,
                          int canvasWidth, int canvasHeight,
                          double customWidth, double customHeight) {
    if (name == "custom") return {customWidth, customHeight};
    const PageSize ps = namedPageSize(name);
    if (canvasWidth > canvasHeight) return {ps.height, ps.width};
    return ps;
  }

  SizePx defaultBlankSizePx(const PageSize& page, double dpi) {
    const auto toPx = [dpi](double cm) {
      const int px = static_cast<int>(cm / 2.54 * dpi + 0.5);
      return px < 1 ? 1 : px;
    };
    return {toPx(page.width), toPx(page.height)};
  }

  Point pixelToPageRaw(double x, double y,
                       const PageSize& dims, int canvasWidth, int canvasHeight) {
    Point p;
    p.x = (canvasWidth  != 0) ? (dims.width  / canvasWidth)  * x : 0.0;
    p.y = (canvasHeight != 0) ? (dims.height / canvasHeight) * y : 0.0;
    return p;
  }

}

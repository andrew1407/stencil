#pragma once
#include "models.hpp"
#include <string>

// Pixel <-> page (cm) conversion. Port of getPageDimensions / pixelToPageCoords
// from browser/js/core/drawingApp.js plus the PAGE_SIZES table from
// browser/js/config/constants.json. Pure: formula transforms (see formulaParser)
// are composed by the caller, exactly as the browser app does.
namespace stencil::core {

  struct PageSize {
    double width = 0.0;   // cm
    double height = 0.0;  // cm
  };

  struct SizePx {
    int width = 0;
    int height = 0;
  };

  // Named page sizes (cm) — the full ISO 216 A/B + ISO 269 C series, "A0".."C10"
  // (exact case-sensitive match). Returns {0,0} for an unknown name.
  PageSize namedPageSize(const std::string& name);

  // Space-separated canonical page-format names ("A0 A1 … A10 B0 … B10 C0 … C10"),
  // in that canonical series order. Excludes "custom". Static storage — never freed.
  const char* pageFormatNames();

  // Page dimensions for the current image, swapping to landscape when the image
  // is wider than tall. `name == "custom"` uses the provided custom dimensions.
  PageSize pageDimensions(const std::string& name,
                          int canvasWidth, int canvasHeight,
                          double customWidth, double customHeight);

  // Raw pixel -> page (cm) conversion, before any f(x,y) formula transform.
  Point pixelToPageRaw(double x, double y,
                       const PageSize& dims, int canvasWidth, int canvasHeight);

  // Default pixel size for a generated blank image: the page (cm) rendered at
  // `dpi` (CSS 96 by default), clamped to at least 1px per side. Port of
  // defaultBlankSizePx in browser/js/core/layout.js.
  SizePx defaultBlankSizePx(const PageSize& page, double dpi = 96.0);

}

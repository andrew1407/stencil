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

  // "A0".."C10", case-sensitive; {0,0} for an unknown name.
  PageSize namedPageSize(const std::string& name);

  // Space-separated, in series order, excluding "custom". Static storage.
  const char* pageFormatNames();

  // Swaps to landscape when the image is wider than tall; "custom" passes custom* through.
  PageSize pageDimensions(const std::string& name,
                          int canvasWidth, int canvasHeight,
                          double customWidth, double customHeight);

  // Before any f(x,y) formula transform.
  Point pixelToPageRaw(double x, double y,
                       const PageSize& dims, int canvasWidth, int canvasHeight);

  // Page (cm) at `dpi` (CSS 96), at least 1px per side. Port of defaultBlankSizePx
  // in browser/js/core/layout.js.
  SizePx defaultBlankSizePx(const PageSize& page, double dpi = 96.0);

}

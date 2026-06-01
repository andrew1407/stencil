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

  // Named page sizes (cm). Returns {0,0} for an unknown name.
  PageSize namedPageSize(const std::string& name);

  // Page dimensions for the current image, swapping to landscape when the image
  // is wider than tall. `name == "custom"` uses the provided custom dimensions.
  PageSize pageDimensions(const std::string& name,
                          int canvasWidth, int canvasHeight,
                          double customWidth, double customHeight);

  // Raw pixel -> page (cm) conversion, before any f(x,y) formula transform.
  Point pixelToPageRaw(double x, double y,
                       const PageSize& dims, int canvasWidth, int canvasHeight);

}

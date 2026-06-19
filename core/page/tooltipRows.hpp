#pragma once
#include "models.hpp"
#include "pageMetrics.hpp"
#include <string>
#include <utility>
#include <vector>

// Pure builder for the hover-tooltip's coordinate rows. Port of the show() rows
// in browser/js/ui/tooltip.js (Pixel / Page (cm) / To edge (cm)), kept GUI-free
// so it is testable and shared with the WebAssembly build. Page (cm) values are
// the already-formula-applied page coords; "To edge" = pageDim - pageCoord.
namespace stencil::core {

  struct TooltipRowFlags {
    bool showScreen = true;  // "Pixel" row     (tooltipShowScreen)
    bool showPage = true;    // "Page (cm)" row (tooltipShowPage)
    bool showCoords = true;  // "To edge (cm)"  (tooltipShowCoords)
  };

  // Display unit for the page/to-edge rows. The model is always cm; `factor`
  // scales cm into the shown unit (1.0 = cm, 1/2.54 = inches) and `label` is the
  // suffix used in the row titles ("Page (cm)" vs "Page (in)").
  struct UnitFormat {
    double factor = 1.0;
    std::string label = "cm";
  };

  // Build the (label, value) rows for a single point. `pixel` is image-space px;
  // `page` is the page (cm) coord (post-formula); `dims` gives page size for the
  // to-edge computation. Values are rounded/formatted exactly like tooltip.js
  // (px rounded to int "x, y"; lengths to 2 decimals "x.xx, y.yy" in `unit`).
  std::vector<std::pair<std::string, std::string>> buildTooltipRows(
      const Point& pixel, const Point& page, const PageSize& dims,
      const TooltipRowFlags& flags, const UnitFormat& unit = {});

}

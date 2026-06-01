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

  // Build the (label, value) rows for a single point. `pixel` is image-space px;
  // `page` is the page (cm) coord (post-formula); `dims` gives page size for the
  // to-edge computation. Values are rounded/formatted exactly like tooltip.js
  // (px rounded to int "x, y"; cm to 2 decimals "x.xx, y.yy").
  std::vector<std::pair<std::string, std::string>> buildTooltipRows(
      const Point& pixel, const Point& page, const PageSize& dims,
      const TooltipRowFlags& flags);

}

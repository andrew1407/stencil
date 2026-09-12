#pragma once
#include "models.hpp"
#include "pageMetrics.hpp"
#include <string>
#include <utility>
#include <vector>

// The hover-tooltip's coordinate rows. Port of the show() rows in
// browser/js/ui/tooltip.js (Pixel / Page (cm) / To edge (cm)); `page` is the
// post-formula page coord and "To edge" = pageDim - pageCoord.
namespace stencil::core {

  struct TooltipRowFlags {
    bool showScreen = true;  // "Pixel" row     (tooltipShowScreen)
    bool showPage = true;    // "Page (cm)" row (tooltipShowPage)
    bool showCoords = true;  // "To edge (cm)"  (tooltipShowCoords)
  };

  // The model is always cm; `factor` scales it into the shown unit (1/2.54 = inches).
  struct UnitFormat {
    double factor = 1.0;
    std::string label = "cm";
  };

  // Formatted like tooltip.js: px rounded to int "x, y", lengths "x.xx, y.yy".
  std::vector<std::pair<std::string, std::string>> buildTooltipRows(
      const Point& pixel, const Point& page, const PageSize& dims,
      const TooltipRowFlags& flags, const UnitFormat& unit = {});

}

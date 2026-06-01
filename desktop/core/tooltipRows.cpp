#include "tooltipRows.hpp"
#include <cmath>
#include <sstream>

namespace stencil::core {

  namespace {
    // "x, y" with x,y rounded to the nearest integer (matches Math.round).
    std::string pxPair(double x, double y) {
      std::ostringstream os;
      os << static_cast<long long>(std::llround(x)) << ", "
         << static_cast<long long>(std::llround(y));
      return os.str();
    }
    // "x.xx, y.yy" fixed to 2 decimals (matches Number.toFixed(2)).
    std::string cmPair(double x, double y) {
      std::ostringstream os;
      os.setf(std::ios::fixed);
      os.precision(2);
      os << x << ", " << y;
      return os.str();
    }
  }  // namespace

  // Port of browser/js/ui/tooltip.js show() row construction.
  std::vector<std::pair<std::string, std::string>> buildTooltipRows(
      const Point& pixel, const Point& page, const PageSize& dims,
      const TooltipRowFlags& flags) {
    std::vector<std::pair<std::string, std::string>> rows;
    if (flags.showScreen)
      rows.emplace_back("Pixel", pxPair(pixel.x, pixel.y));
    if (flags.showPage)
      rows.emplace_back("Page (cm)", cmPair(page.x, page.y));
    if (flags.showCoords)  // tailX = ps.width - pageX, tailY = ps.height - pageY
      rows.emplace_back("To edge (cm)",
                        cmPair(dims.width - page.x, dims.height - page.y));
    return rows;
  }

}

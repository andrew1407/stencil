#include "zoomPan.hpp"
#include <algorithm>

namespace stencil::core {

  // Port of browser/js/core/zoomPan.js clamp.
  double clampScale(double scale) {
    return std::max(kZoomMin, std::min(kZoomMax, scale));
  }

  // Port of browser/js/core/zoomPan.js zoomToward.
  AnchoredZoom anchoredZoom(double scrollLeft, double scrollTop, double cursorX,
                            double cursorY, double oldScale, double newScale) {
    newScale = clampScale(newScale);
    const double contentX = cursorX + scrollLeft;
    const double contentY = cursorY + scrollTop;
    const double imgX = contentX / oldScale;
    const double imgY = contentY / oldScale;
    AnchoredZoom r;
    r.scale = newScale;
    r.scrollLeft = imgX * newScale - cursorX;
    r.scrollTop = imgY * newScale - cursorY;
    return r;
  }

  // Port of browser/js/core/zoomPan.js zoomToRect.
  RectZoom rectZoom(double x1, double y1, double rectW, double rectH,
                    double availW, double availH) {
    RectZoom r;
    r.scale = clampScale(std::min({availW / rectW, availH / rectH, kZoomMax}));
    r.scrollLeft = std::max(0.0, x1 * r.scale - (availW - rectW * r.scale) / 2.0);
    r.scrollTop = std::max(0.0, y1 * r.scale - (availH - rectH * r.scale) / 2.0);
    return r;
  }

}

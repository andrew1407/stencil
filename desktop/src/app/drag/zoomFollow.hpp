#pragma once
// The zoom a toolbar zoom drag follows: z0·e^(±k·d) for the pointer d px from the icon's centre,
// and the fit drag's steady step while the pointer rests on − or +.
// Browser twin: browser/js/ui/drag/zoomDrag.js (ZOOM_DRAG_K, zoomAtDistance).
#include <cmath>
#include "zoomCore.hpp"

namespace stencil::gui {

  // ln 8 / 300: about 300 px of drag spans ×8 or ÷8.
  inline constexpr double ZOOM_DRAG_K = 0.006931471805599453;
  // The browser's hold-zoom tick (ui/bindings/viewport/holdZoom.js HOLD_REPEAT_MS); the step is the
  // core's ZOOM_HOLD_STEP. The desktop's − and + have no hold of their own.
  inline constexpr int HOLD_ZOOM_TICK_MS = 90;

  // `sign` +1 zooms in, -1 out. Unclamped: the window's setZoom holds it to the zoom range.
  inline double zoomFromDistance(double z0, double d, int sign) {
    return z0 * std::exp(sign * ZOOM_DRAG_K * d);
  }

  inline double holdZoomStep(double z, int sign) { return z + sign * core::zoomStepAt(z, core::ZOOM_HOLD_STEP); }

}  // namespace stencil::gui

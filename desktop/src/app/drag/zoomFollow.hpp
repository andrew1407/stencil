#pragma once
// The zoom a toolbar zoom drag follows: z0·e^(±k·d) for the pointer d px from the icon's centre,
// and the fit drag's steady step while the pointer rests on − or +.
// Browser twin: browser/js/ui/drag/zoomDrag.js (ZOOM_DRAG_K, zoomAtDistance).
#include <cmath>

namespace stencil::gui {

  // ln 8 / 300: about 300 px of drag spans ×8 or ÷8.
  inline constexpr double ZOOM_DRAG_K = 0.006931471805599453;
  // The browser's hold-zoom rate (ui/bindings/viewport/holdZoom.js HOLD_STEP, HOLD_REPEAT_MS), in
  // scale units per tick; the desktop's − and + have no hold of their own.
  inline constexpr double HOLD_ZOOM_STEP = 0.05;
  inline constexpr int HOLD_ZOOM_TICK_MS = 90;

  // `sign` +1 zooms in, -1 out. Unclamped: the window's setZoom holds it to the zoom range.
  inline double zoomFromDistance(double z0, double d, int sign) {
    return z0 * std::exp(sign * ZOOM_DRAG_K * d);
  }

  inline double holdZoomStep(double z, int sign) { return z + sign * HOLD_ZOOM_STEP; }

}  // namespace stencil::gui

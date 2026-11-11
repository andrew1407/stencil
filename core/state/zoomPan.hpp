#pragma once

// Zoom/pan math. Port of browser/js/core/zoom/pan.js (clamp / zoomToward / rectZoom).
namespace stencil::core {

  // zoom/pan.js MIN/MAX/STEP/STEP_FAST.
  constexpr double ZOOM_MIN = 0.05;
  constexpr double ZOOM_MAX = 32.0;   // 3200% — headroom to magnify small images/pixels
  constexpr double ZOOM_STEP = 0.1;
  constexpr double ZOOM_STEP_FAST = 0.3;
  // One − / + press or zoom key, a double press or Shift, and one tick of a held press, on every surface.
  constexpr double ZOOM_PRESS_STEP = 0.25;
  constexpr double ZOOM_PRESS_STEP_BIG = 1.0;
  constexpr double ZOOM_HOLD_STEP = 0.05;

  double clampScale(double scale);
  // `step` scale units as given up to 100%, times the scale above it: a step reads alike at 940% and 94%.
  double zoomStepAt(double scale, double step);

  struct AnchoredZoom {
    double scale = 1.0;
    double scrollLeft = 0.0;
    double scrollTop = 0.0;
  };

  // Toward-cursor zoom: the scroll offsets keep the image pixel under (cursorX, cursorY),
  // relative to the viewport top-left, fixed.
  AnchoredZoom anchoredZoom(double scrollLeft, double scrollTop, double cursorX,
                            double cursorY, double oldScale, double newScale);

  // A swept image-space rect fills the availW x availH viewport, centred, capped at ZOOM_MAX.
  struct RectZoom {
    double scale = 1.0;
    double scrollLeft = 0.0;
    double scrollTop = 0.0;
  };
  RectZoom rectZoom(double x1, double y1, double rectW, double rectH,
                    double availW, double availH);

}

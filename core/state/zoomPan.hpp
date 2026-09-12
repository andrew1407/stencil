#pragma once

namespace stencil::core {

  // Zoom/pan math. Port of browser/js/core/zoomPan.js (clamp / zoomToward / zoomToRect).

  // zoomPan.js MIN/MAX/STEP/STEP_FAST.
  constexpr double kZoomMin = 0.05;
  constexpr double kZoomMax = 32.0;   // 3200% — headroom to magnify small images/pixels
  constexpr double kZoomStep = 0.1;
  constexpr double kZoomStepFast = 0.3;

  double clampScale(double scale);

  struct AnchoredZoom {
    double scale = 1.0;
    double scrollLeft = 0.0;
    double scrollTop = 0.0;
  };

  // Toward-cursor zoom: the scroll offsets keep the image pixel under (cursorX, cursorY),
  // relative to the viewport top-left, fixed.
  AnchoredZoom anchoredZoom(double scrollLeft, double scrollTop, double cursorX,
                            double cursorY, double oldScale, double newScale);

  // A swept image-space rect fills the availW x availH viewport, centred, capped at kZoomMax.
  struct RectZoom {
    double scale = 1.0;
    double scrollLeft = 0.0;
    double scrollTop = 0.0;
  };
  RectZoom rectZoom(double x1, double y1, double rectW, double rectH,
                    double availW, double availH);

}

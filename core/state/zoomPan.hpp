#pragma once

namespace stencil::core {

  // Shared zoom/pan math, ported from browser/js/core/zoomPan.js so the desktop
  // (Qt) and a future WebAssembly browser build share identical behavior. Pure
  // STL — no Qt, no I/O.

  // Zoom limits + wheel steps (zoomPan.js MIN/MAX/STEP/STEP_FAST).
  constexpr double kZoomMin = 0.05;
  constexpr double kZoomMax = 32.0;   // 3200% — headroom to magnify small images/pixels
  constexpr double kZoomStep = 0.1;
  constexpr double kZoomStepFast = 0.3;

  // Clamp a scale into [kZoomMin, kZoomMax] (zoomPan.js clamp).
  double clampScale(double scale);

  // Result of an anchored (toward-cursor) zoom: the new scale plus the scroll
  // offsets that keep the image pixel under the cursor fixed.
  struct AnchoredZoom {
    double scale = 1.0;
    double scrollLeft = 0.0;
    double scrollTop = 0.0;
  };

  // Port of zoomPan.js zoomToward: given the current scroll offsets, the cursor
  // position relative to the viewport top-left, and a target scale, return the
  // new scale (clamped) and the scroll offsets keeping the focal pixel fixed.
  //   contentX = cursorX + scrollLeft;  imgX = contentX / oldScale;
  //   scrollLeft' = imgX * newScale - cursorX  (same for Y).
  AnchoredZoom anchoredZoom(double scrollLeft, double scrollTop, double cursorX,
                            double cursorY, double oldScale, double newScale);

  // Port of zoomPan.js zoomToRect: zoom so a swept image-space rect fills the
  // viewport (capped at kZoomMax) and is centered. x1/y1 = rect top-left in image
  // space; rectW/rectH = rect size in image space; availW/availH = viewport size.
  //   newScale = min(availW/rectW, availH/rectH, MAX)
  //   scrollLeft = max(0, x1*newScale - (availW - rectW*newScale)/2)  (same Y).
  struct RectZoom {
    double scale = 1.0;
    double scrollLeft = 0.0;
    double scrollTop = 0.0;
  };
  RectZoom rectZoom(double x1, double y1, double rectW, double rectH,
                    double availW, double availH);

}

#pragma once
// A picture arriving or leaving under the two non-dust modes (browser ui/motion/control/canvasSlide.js):
// Sliding pours it out of, and back into, the middle of the view's bottom edge on the window genie;
// Transition wipes it in from the view's bottom edge upward and away from its top down. Any other mode plays nothing.
class QWidget;

namespace stencil::support {

  // After a load: plays over `canvas` once the owner has fitted it (next turn of the loop).
  void playCanvasArrival(QWidget* canvas);
  // Before a clear: photographs `canvas` now and plays the exit over its viewport.
  void playCanvasLeave(QWidget* canvas);
  // By when every mode's arrival or exit has landed (ms): the dust's clear runs 760 (dust/dustTunings.hpp
  // CANVAS_DUST_MS), Sliding's pour 840 (ARRIVE_MS). A dialog's backdrop follows the canvas that long.
  inline constexpr int CANVAS_SETTLE_MS = 900;

}  // namespace stencil::support

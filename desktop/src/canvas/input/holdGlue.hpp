#pragma once
#include "canvasCore.hpp"
#include "pointerTuning.hpp"

#include <QElapsedTimer>
#include <QPoint>
#include <QTimer>

// Hold-to-draw's state on the canvas (browser holdDraw.js): `ctl` is the pure controller, `timer`
// ticks it while a hold is engaged, `clock` supplies monotonic ms.
namespace stencil::gui {

  struct HoldGlue {
    core::HoldDrawController ctl{double(pointerTuning::table().holdDelayMs),
                                 pointerTuning::table().holdMoveTolerancePx,
                                 pointerTuning::table().holdRearmDistancePx};
    QTimer timer;
    QElapsedTimer clock;
    int delayMs = pointerTuning::table().holdDelayMs;
    bool hasPreview = false;
    // A hold stroke extending BACKWARD from the first point prepends (index 0).
    bool prepend = false;
    core::Point preview;
    QPoint pressPos;
  };

}  // namespace stencil::gui

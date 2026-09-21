#pragma once

// Hold-to-draw gesture state machine. Port of the HoldDrawController in
// browser/js/core/draw/holdDraw.js: time-injected (monotonic ms), coordinates in host
// screen space so tolerances stay zoom-independent; the host owns timers and rendering.

namespace stencil::core {

  enum class HoldAction {
    NONE,     // nothing to do
    ARMED,    // pointerDown accepted; hold timer running
    ABORT,    // moved too far before the hold completed → treat as click/drag
    START,    // hold completed → enable drawing + drop first point at (x, y)
    DROP,     // dwell completed → drop a point at (x, y)
    PREVIEW,  // cursor moved while drawing → update the ghost line to (x, y)
    COMMIT,   // released after drawing → commit the line + disable drawing
  };

  struct HoldEvent {
    HoldAction action = HoldAction::NONE;
    double x = 0.0;
    double y = 0.0;
  };

  enum class HoldState { IDLE, ARMED, DRAWING, ABORTED };

  class HoldDrawController {
  public:
    explicit HoldDrawController(double holdDelay = 500.0,
                                double moveTolerance = 6.0,
                                double rearmDistance = 10.0)
        : holdDelay(holdDelay < 0.0 ? 0.0 : holdDelay),
          moveTol(moveTolerance),
          rearm(rearmDistance) {}

    HoldState getState() const { return state; }
    bool active() const { return state == HoldState::DRAWING; }
    bool engaged() const {
      return state == HoldState::ARMED || state == HoldState::DRAWING;
    }
    double getHoldDelay() const { return holdDelay; }
    void setHoldDelay(double ms) {
      if (ms >= 0.0) holdDelay = ms;
    }

    void cancel() {
      state = HoldState::IDLE;
      armedForDrop = false;
    }

    HoldEvent pointerDown(double x, double y, double t);
    HoldEvent pointerMove(double x, double y, double t);
    HoldEvent tick(double t);
    HoldEvent pointerUp(double t);

  private:
    HoldState state = HoldState::IDLE;
    double holdDelay;
    double moveTol;
    double rearm;
    double pressX = 0.0, pressY = 0.0, pressT = 0.0;
    double stillX = 0.0, stillY = 0.0, stillSince = 0.0;
    double lastDropX = 0.0, lastDropY = 0.0;
    bool armedForDrop = false;
  };

}

#pragma once

// Hold-to-draw gesture state machine (GUI-free, STL only). Port of the
// HoldDrawController in browser/js/core/holdDraw.js — the host owns timers,
// coordinates and rendering; this class is pure and time-injected (monotonic ms),
// coordinates in host screen space so tolerances stay zoom-independent.

namespace stencil::core {

  enum class HoldAction {
    None,     // nothing to do
    Armed,    // pointerDown accepted; hold timer running
    Abort,    // moved too far before the hold completed → treat as click/drag
    Start,    // hold completed → enable drawing + drop first point at (x, y)
    Drop,     // dwell completed → drop a point at (x, y)
    Preview,  // cursor moved while drawing → update the ghost line to (x, y)
    Commit,   // released after drawing → commit the line + disable drawing
  };

  struct HoldEvent {
    HoldAction action = HoldAction::None;
    double x = 0.0;
    double y = 0.0;
  };

  enum class HoldState { Idle, Armed, Drawing, Aborted };

  class HoldDrawController {
  public:
    explicit HoldDrawController(double holdDelay = 500.0,
                                double moveTolerance = 6.0,
                                double rearmDistance = 10.0)
        : holdDelay_(holdDelay < 0.0 ? 0.0 : holdDelay),
          moveTol_(moveTolerance),
          rearm_(rearmDistance) {}

    HoldState state() const { return state_; }
    bool active() const { return state_ == HoldState::Drawing; }
    bool engaged() const {
      return state_ == HoldState::Armed || state_ == HoldState::Drawing;
    }
    double holdDelay() const { return holdDelay_; }
    void setHoldDelay(double ms) {
      if (ms >= 0.0) holdDelay_ = ms;
    }

    void cancel() {
      state_ = HoldState::Idle;
      armedForDrop_ = false;
    }

    HoldEvent pointerDown(double x, double y, double t);
    HoldEvent pointerMove(double x, double y, double t);
    HoldEvent tick(double t);
    HoldEvent pointerUp(double t);

  private:
    HoldState state_ = HoldState::Idle;
    double holdDelay_;
    double moveTol_;
    double rearm_;
    double pressX_ = 0.0, pressY_ = 0.0, pressT_ = 0.0;
    double stillX_ = 0.0, stillY_ = 0.0, stillSince_ = 0.0;
    double lastDropX_ = 0.0, lastDropY_ = 0.0;
    bool armedForDrop_ = false;
  };

}

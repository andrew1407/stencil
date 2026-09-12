#include "holdDraw.hpp"
#include <cmath>

namespace stencil::core {

  namespace {
    double dist(double ax, double ay, double bx, double by) {
      return std::hypot(ax - bx, ay - by);
    }
  }

  HoldEvent HoldDrawController::pointerDown(double x, double y, double t) {
    state_ = HoldState::ARMED;
    pressX_ = x; pressY_ = y; pressT_ = t;
    stillX_ = x; stillY_ = y; stillSince_ = t;
    armedForDrop_ = false;
    return HoldEvent{HoldAction::ARMED, 0.0, 0.0};
  }

  HoldEvent HoldDrawController::pointerMove(double x, double y, double t) {
    if (state_ == HoldState::ARMED) {
      // Moving away before the hold fires = a real click/drag, not a hold.
      if (dist(x, y, pressX_, pressY_) > moveTol_) {
        state_ = HoldState::ABORTED;
        return HoldEvent{HoldAction::ABORT, 0.0, 0.0};
      }
      return HoldEvent{HoldAction::NONE, 0.0, 0.0};
    }
    if (state_ == HoldState::DRAWING) {
      // New dwell window whenever the cursor leaves the current rest neighborhood.
      if (dist(x, y, stillX_, stillY_) > moveTol_) {
        stillX_ = x; stillY_ = y; stillSince_ = t;
      }
      // Re-arm a drop only once the cursor leaves the last dropped point's vicinity.
      if (dist(x, y, lastDropX_, lastDropY_) > rearm_) armedForDrop_ = true;
      return HoldEvent{HoldAction::PREVIEW, x, y};
    }
    return HoldEvent{HoldAction::NONE, 0.0, 0.0};
  }

  HoldEvent HoldDrawController::tick(double t) {
    if (state_ == HoldState::ARMED) {
      if (t - pressT_ >= holdDelay_) {
        state_ = HoldState::DRAWING;
        lastDropX_ = pressX_; lastDropY_ = pressY_;
        stillX_ = pressX_; stillY_ = pressY_; stillSince_ = t;
        armedForDrop_ = false;
        return HoldEvent{HoldAction::START, pressX_, pressY_};
      }
      return HoldEvent{HoldAction::NONE, 0.0, 0.0};
    }
    if (state_ == HoldState::DRAWING) {
      if (armedForDrop_ && t - stillSince_ >= holdDelay_) {
        armedForDrop_ = false;
        lastDropX_ = stillX_; lastDropY_ = stillY_;
        stillSince_ = t;
        return HoldEvent{HoldAction::DROP, stillX_, stillY_};
      }
      return HoldEvent{HoldAction::NONE, 0.0, 0.0};
    }
    return HoldEvent{HoldAction::NONE, 0.0, 0.0};
  }

  HoldEvent HoldDrawController::pointerUp(double /*t*/) {
    const bool wasDrawing = state_ == HoldState::DRAWING;
    state_ = HoldState::IDLE;
    armedForDrop_ = false;
    return wasDrawing ? HoldEvent{HoldAction::COMMIT, 0.0, 0.0}
                      : HoldEvent{HoldAction::NONE, 0.0, 0.0};
  }

}

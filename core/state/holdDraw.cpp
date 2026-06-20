#include "holdDraw.hpp"
#include <cmath>

namespace stencil::core {

  namespace {
    double dist(double ax, double ay, double bx, double by) {
      return std::hypot(ax - bx, ay - by);
    }
  }

  HoldEvent HoldDrawController::pointerDown(double x, double y, double t) {
    state_ = HoldState::Armed;
    pressX_ = x; pressY_ = y; pressT_ = t;
    stillX_ = x; stillY_ = y; stillSince_ = t;
    armedForDrop_ = false;
    return HoldEvent{HoldAction::Armed, 0.0, 0.0};
  }

  HoldEvent HoldDrawController::pointerMove(double x, double y, double t) {
    if (state_ == HoldState::Armed) {
      // Moving away before the hold fires = a real click/drag, not a hold.
      if (dist(x, y, pressX_, pressY_) > moveTol_) {
        state_ = HoldState::Aborted;
        return HoldEvent{HoldAction::Abort, 0.0, 0.0};
      }
      return HoldEvent{HoldAction::None, 0.0, 0.0};
    }
    if (state_ == HoldState::Drawing) {
      // New dwell window whenever the cursor leaves the current rest neighborhood.
      if (dist(x, y, stillX_, stillY_) > moveTol_) {
        stillX_ = x; stillY_ = y; stillSince_ = t;
      }
      // Re-arm a drop only once the cursor leaves the last dropped point's vicinity.
      if (dist(x, y, lastDropX_, lastDropY_) > rearm_) armedForDrop_ = true;
      return HoldEvent{HoldAction::Preview, x, y};
    }
    return HoldEvent{HoldAction::None, 0.0, 0.0};
  }

  HoldEvent HoldDrawController::tick(double t) {
    if (state_ == HoldState::Armed) {
      if (t - pressT_ >= holdDelay_) {
        state_ = HoldState::Drawing;
        lastDropX_ = pressX_; lastDropY_ = pressY_;
        stillX_ = pressX_; stillY_ = pressY_; stillSince_ = t;
        armedForDrop_ = false;
        return HoldEvent{HoldAction::Start, pressX_, pressY_};
      }
      return HoldEvent{HoldAction::None, 0.0, 0.0};
    }
    if (state_ == HoldState::Drawing) {
      if (armedForDrop_ && t - stillSince_ >= holdDelay_) {
        armedForDrop_ = false;
        lastDropX_ = stillX_; lastDropY_ = stillY_;
        stillSince_ = t;
        return HoldEvent{HoldAction::Drop, stillX_, stillY_};
      }
      return HoldEvent{HoldAction::None, 0.0, 0.0};
    }
    return HoldEvent{HoldAction::None, 0.0, 0.0};
  }

  HoldEvent HoldDrawController::pointerUp(double /*t*/) {
    const bool wasDrawing = state_ == HoldState::Drawing;
    state_ = HoldState::Idle;
    armedForDrop_ = false;
    return wasDrawing ? HoldEvent{HoldAction::Commit, 0.0, 0.0}
                      : HoldEvent{HoldAction::None, 0.0, 0.0};
  }

}

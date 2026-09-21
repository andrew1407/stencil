#include "holdDraw.hpp"
#include <cmath>

namespace stencil::core {

  namespace {
    double dist(double ax, double ay, double bx, double by) {
      return std::hypot(ax - bx, ay - by);
    }
  }

  HoldEvent HoldDrawController::pointerDown(double x, double y, double t) {
    state = HoldState::ARMED;
    pressX = x; pressY = y; pressT = t;
    stillX = x; stillY = y; stillSince = t;
    armedForDrop = false;
    return HoldEvent{HoldAction::ARMED, 0.0, 0.0};
  }

  HoldEvent HoldDrawController::pointerMove(double x, double y, double t) {
    if (state == HoldState::ARMED) {
      // Moving away before the hold fires = a real click/drag, not a hold.
      if (dist(x, y, pressX, pressY) > moveTol) {
        state = HoldState::ABORTED;
        return HoldEvent{HoldAction::ABORT, 0.0, 0.0};
      }
      return HoldEvent{HoldAction::NONE, 0.0, 0.0};
    }
    if (state == HoldState::DRAWING) {
      // New dwell window whenever the cursor leaves the current rest neighborhood.
      if (dist(x, y, stillX, stillY) > moveTol) {
        stillX = x; stillY = y; stillSince = t;
      }
      // Re-arm a drop only once the cursor leaves the last dropped point's vicinity.
      if (dist(x, y, lastDropX, lastDropY) > rearm) armedForDrop = true;
      return HoldEvent{HoldAction::PREVIEW, x, y};
    }
    return HoldEvent{HoldAction::NONE, 0.0, 0.0};
  }

  HoldEvent HoldDrawController::tick(double t) {
    if (state == HoldState::ARMED) {
      if (t - pressT >= holdDelay) {
        state = HoldState::DRAWING;
        lastDropX = pressX; lastDropY = pressY;
        stillX = pressX; stillY = pressY; stillSince = t;
        armedForDrop = false;
        return HoldEvent{HoldAction::START, pressX, pressY};
      }
      return HoldEvent{HoldAction::NONE, 0.0, 0.0};
    }
    if (state == HoldState::DRAWING) {
      if (armedForDrop && t - stillSince >= holdDelay) {
        armedForDrop = false;
        lastDropX = stillX; lastDropY = stillY;
        stillSince = t;
        return HoldEvent{HoldAction::DROP, stillX, stillY};
      }
      return HoldEvent{HoldAction::NONE, 0.0, 0.0};
    }
    return HoldEvent{HoldAction::NONE, 0.0, 0.0};
  }

  HoldEvent HoldDrawController::pointerUp(double /*t*/) {
    const bool wasDrawing = state == HoldState::DRAWING;
    state = HoldState::IDLE;
    armedForDrop = false;
    return wasDrawing ? HoldEvent{HoldAction::COMMIT, 0.0, 0.0}
                      : HoldEvent{HoldAction::NONE, 0.0, 0.0};
  }

}

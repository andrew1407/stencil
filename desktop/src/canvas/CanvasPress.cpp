#include "CanvasWidget.hpp"
#include "CanvasWidget.hpp"

#include <QMouseEvent>

// mousePressEvent: which gesture a press starts.

namespace stencil::gui {

  // Precedence is load-bearing: Right -> Middle pan -> Alt+Left -> Shift+Left zoom-rect ->
  // Left{Ctrl, rect-draw, select, rect-noop, continuation, close, append}.
  void CanvasWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
      // macOS delivers Ctrl+Left as a right press: Alt with it is the pull-out gesture, not a menu.
      if (!image_.isNull() && (event->modifiers() & Qt::AltModifier) &&
          (event->modifiers() & Qt::ControlModifier) && !compareReadOnly()) {
        if (beginPullOut(toImageSpace(event->pos().x(), event->pos().y()))) return;
        return;   // nothing under the cursor: still the gesture, still no menu
      }
      emit contextRequested(event->globalPosition().toPoint());
      return;
    }
    if (image_.isNull()) {
      // Only the CARD opens the creator; nothing is clickable while the hint is held back or unpainted.
      if (event->button() == Qt::LeftButton && !idleHintHidden_ &&
          idleCardRect_.contains(event->position()))
        emit blankImageRequested();
      return;
    }

    const auto mods = event->modifiers();

    // Divider press is checked first so it wins over hits underneath; split modes only (not held).
    if (event->button() == Qt::LeftButton && mods == Qt::NoModifier && !compareHoldOriginal_ &&
        nearCompareDivider(event->pos())) {
      draggingCompareSplit_ = true;
      setCursor(compareMode_ == "vertical" ? Qt::SplitHCursor : Qt::SplitVCursor);
      return;
    }

    // Compare view is read-only; only navigation stays.
    if (compareReadOnly()) {
      if (event->button() == Qt::MiddleButton ||
          (event->button() == Qt::LeftButton && (mods & Qt::AltModifier))) {
        panning_ = true;
        lastPanPos_ = event->globalPosition().toPoint();
        setCursor(Qt::ClosedHandCursor);
      }
      return;
    }

    if (event->button() == Qt::MiddleButton) {
      panning_ = true;
      // Pan anchor in GLOBAL coords: panBy() slides this widget under the cursor, so widget-space
      // positions would feed back into the next delta.
      lastPanPos_ = event->globalPosition().toPoint();
      setCursor(Qt::ClosedHandCursor);
      return;
    }

    if (event->button() == Qt::LeftButton && (mods & Qt::AltModifier)) {
      beginAltDrag(toImageSpace(event->pos().x(), event->pos().y()), mods,
                   event->globalPosition().toPoint());
      return;
    }

    // Ctrl+Shift multi-select must precede the plain Shift zoom-rect branch.
    if (event->button() == Qt::LeftButton && (mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier)) {
      toggleLineSelection(toImageSpace(event->pos().x(), event->pos().y()));
      return;
    }

    if (event->button() == Qt::LeftButton && (mods & Qt::ShiftModifier)) {
      beginZoomRect(event->pos());
      return;
    }

    if (event->button() == Qt::LeftButton) {
      const core::Point ip = toImageSpace(event->pos().x(), event->pos().y());
      if (mods & Qt::ControlModifier) {
        if (handleCtrlClick(ip)) return;
      }
      // Hold-to-draw arms only when not drawing, unmodified, and never with the rect tool (browser
      // inputController.js holdDrawEligible). handleDrawingClick runs first so a quick click still selects.
      const bool eligibleHold =
          !isDrawing_ && mods == Qt::NoModifier && drawMode_ != DrawMode::RECT;
      handleDrawingClick(ip, mods, event->pos());
      if (eligibleHold && !isDrawing_) beginHold(event->pos());
    }
  }

}  // namespace stencil::gui

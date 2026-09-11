#include "canvasWidget.hpp"
#include "canvasWidget.hpp"

#include <QMouseEvent>

// mousePressEvent: which gesture a press starts.

namespace stencil::gui {

  // Flat dispatch; precedence is load-bearing: RightButton -> MiddleButton pan ->
  // Alt+Left drag/pan -> Shift+Left zoom-rect -> Left{Ctrl, rect-draw, select,
  // rect-noop, continuation, close, append}. Alt/Left branches use image space.
  void CanvasWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
      // macOS delivers Ctrl+Left as a right button press, so the Alt+Ctrl pull-out drag
      // arrived here as a context-menu request. Alt with it means the gesture, not a menu;
      // a plain right-click, and plain Ctrl+click, still get one.
      if (!image_.isNull() && (event->modifiers() & Qt::AltModifier) &&
          (event->modifiers() & Qt::ControlModifier) && !compareReadOnly()) {
        if (beginPullOut(toImageSpace(event->pos().x(), event->pos().y()))) return;
        return;   // nothing under the cursor: still the gesture, still no menu
      }
      emit contextRequested(event->globalPosition().toPoint());
      return;
    }
    if (image_.isNull()) {
      // Idle: the "＋ Blank image" CARD alone opens the creator — never the empty
      // page around it. While the hint is held back for the clear animation, or
      // before it has been painted, nothing is clickable.
      if (event->button() == Qt::LeftButton && !idleHintHidden_ &&
          idleCardRect_.contains(event->position()))
        emit blankImageRequested();
      return;
    }

    const auto mods = event->modifiers();

    // Compare split divider: plain left-press on the divider starts sliding it. Checked
    // first so it wins over point/line hits underneath, and only when a split mode is the
    // active (not held) view.
    if (event->button() == Qt::LeftButton && mods == Qt::NoModifier && !compareHoldOriginal_ &&
        nearCompareDivider(event->pos())) {
      draggingCompareSplit_ = true;
      setCursor(compareMode_ == "vertical" ? Qt::SplitHCursor : Qt::SplitVCursor);
      return;
    }

    // Compare view is read-only: no drawing/selecting/point-line drags. Only navigation
    // stays — Alt+left / middle-button pan (the divider drag is handled above).
    if (compareReadOnly()) {
      if (event->button() == Qt::MiddleButton ||
          (event->button() == Qt::LeftButton && (mods & Qt::AltModifier))) {
        panning_ = true;
        lastPanPos_ = event->globalPosition().toPoint();
        setCursor(Qt::ClosedHandCursor);
      }
      return;
    }

    // Middle-button always pans (port of drawingApp.js startPan ~757).
    if (event->button() == Qt::MiddleButton) {
      panning_ = true;
      // Track the pan anchor in GLOBAL coords: panBy() scrolls the viewport,
      // sliding this widget under the cursor, so widget-space event->pos() would
      // feed back into the next delta (flicker/jump).
      lastPanPos_ = event->globalPosition().toPoint();
      setCursor(Qt::ClosedHandCursor);
      return;
    }

    if (event->button() == Qt::LeftButton && (mods & Qt::AltModifier)) {
      beginAltDrag(toImageSpace(event->pos().x(), event->pos().y()), mods,
                   event->globalPosition().toPoint());
      return;
    }

    // Ctrl+Shift+left → multi-line select: add/toggle the clicked line. Must precede the plain
    // Shift zoom-rect branch below (which would otherwise swallow Ctrl+Shift). Alt already returned.
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
        // drawing + Ctrl + no segment -> fall through to handleDrawingClick.
      }
      // Hold-to-draw arms only when not already drawing, with no modifiers, and never
      // with the rect tool (a hold there would seed a freehand line — browser
      // inputController.js holdDrawEligible). handleDrawingClick still runs first so a
      // quick click keeps selecting.
      const bool eligibleHold =
          !isDrawing_ && mods == Qt::NoModifier && drawMode_ != DrawMode::Rect;
      handleDrawingClick(ip, mods, event->pos());
      if (eligibleHold && !isDrawing_) beginHold(event->pos());
    }
  }

}  // namespace stencil::gui

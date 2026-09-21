#include "CanvasWidget.hpp"
#include "CanvasWidget.hpp"
#include "hitTest.hpp"

// Hold-to-draw: the delay, the drop and the anchor it continues from.

namespace stencil::gui {

  // hold-to-draw (alternative flow; port of browser holdDraw.js)

  void CanvasWidget::setHoldDrawDelay(int ms) {
    holdDelayMs = std::max(100, std::min(3000, ms));
    hold.setHoldDelay(holdDelayMs);
  }

  double CanvasWidget::holdNowMs() const {
    return static_cast<double>(holdClock.elapsed());
  }

  void CanvasWidget::beginHold(const QPoint& widgetPos) {
    holdPressPos = widgetPos;
    hold.pointerDown(widgetPos.x(), widgetPos.y(), holdNowMs());
    holdTimer.start();
  }

  void CanvasWidget::stopHold() {
    holdTimer.stop();
    hold.cancel();
    if (holdHasPreview) {
      holdHasPreview = false;
      update();
    }
  }

  void CanvasWidget::handleHoldTick() {
    const core::HoldEvent ev = hold.tick(holdNowMs());
    if (ev.action == core::HoldAction::START) holdStart(ev.x, ev.y);
    else if (ev.action == core::HoldAction::DROP) holdDrop(ev.x, ev.y);
  }

  // The target under the press decides: existing point -> continue that line; line body -> insert
  // a point there then continue; empty -> fresh line.
  void CanvasWidget::holdStart(double widgetX, double widgetY) {
    const core::Point ip{widgetX / scale, widgetY / scale};
    const core::HoldTarget t = core::holdDrawTarget(lines, ip.x, ip.y);
    holdPrepend = false;
    if (t.kind == core::HoldTargetKind::CONTINUE_POINT) {
      selectedLineIdx = t.lineIdx;
      selectedPoint = t.ptIdx;
      startDrawingMode();
      // Holding the FIRST point extends the line backward: prepend new points
      // before it (index 0) instead of inserting after it as the second point.
      if (t.ptIdx == 0) { holdPrepend = true; continueInsertIdx = 0; }
    } else if (t.kind == core::HoldTargetKind::INSERT_SEGMENT) {
      insertPointOnSegment(t.lineIdx, t.ptIdx2, ip.x, ip.y);
      startDrawingMode();
    } else {
      selectedLineIdx = -1;
      selectedPoint = -1;
      startDrawingMode();
      currentLine.points.push_back(ip);
      flyInPoint(-1, currentLine, 0);
    }
    holdPreview = ip;
    holdHasPreview = true;
    update();
    emit changed();
    emit selectionChanged();
  }

  // Insert ip into the continued line at the clamped insert cursor + select it. Prepend
  // mode keeps inserting at the head; forward mode advances so points keep appending.
  void CanvasWidget::insertContinuationPoint(const core::Point& ip, bool advance) {
    core::Line& line = lines[continueLineIdx];
    const int at = std::max(
        0, std::min(continueInsertIdx, static_cast<int>(line.points.size())));
    line.points.insert(line.points.begin() + at, ip);
    flyInPoint(continueLineIdx, line, at);
    selectedPoint = at;
    if (advance) continueInsertIdx = at + 1;
  }

  // Dwell completed → drop a point (extends the in-progress / continued line).
  void CanvasWidget::holdDrop(double widgetX, double widgetY) {
    const core::Point ip{widgetX / scale, widgetY / scale};
    // Resting on the stroke's FIRST point closes it, as clicking there does. The shape is committed,
    // so the gesture is over: end it rather than dropping more points into a dead stroke.
    if (tryCloseShapeAt(ip)) {
      stopHold();
      emit selectionChanged();
      return;
    }
    if (continueLineIdx >= 0 &&
        continueLineIdx < static_cast<int>(lines.size())) {
      insertContinuationPoint(ip, /*advance=*/!holdPrepend);
    } else {
      currentLine.points.push_back(ip);
      flyInPoint(-1, currentLine, static_cast<int>(currentLine.points.size()) - 1);
    }
    holdPreview = ip;
    holdHasPreview = true;
    update();
    emit changed();
    emit selectionChanged();
  }

  // Release after a hold stroke → commit the line and disable drawing again.
  void CanvasWidget::holdCommit() {
    holdHasPreview = false;
    holdPrepend = false;
    if (isDrawing) stopDrawingMode();  // commits + emits drawingModeChanged(false)
    update();
  }

  // Origin of the hold-draw preview line: the tail of the in-progress line, or the
  // current insertion tail of the line being extended. nullptr = nothing to anchor.
  const core::Point* CanvasWidget::holdAnchor() const {
    if (!currentLine.points.empty()) return &currentLine.points.back();
    if (continueLineIdx >= 0 &&
        continueLineIdx < static_cast<int>(lines.size())) {
      const std::vector<core::Point>& pts = lines[continueLineIdx].points;
      if (pts.empty()) return nullptr;
      // Prepend: the next point connects to the current head (continueInsertIdx);
      // forward: it connects to the point just before the insertion tail.
      const int idx = holdPrepend ? continueInsertIdx : continueInsertIdx - 1;
      if (idx >= 0 && idx < static_cast<int>(pts.size())) return &pts[idx];
      return &pts.back();
    }
    return nullptr;
  }

}  // namespace stencil::gui

#include "CanvasWidget.hpp"
#include "CanvasWidget.hpp"
#include "hitTest.hpp"

// Hold-to-draw: the delay, the drop and the anchor it continues from.

namespace stencil::gui {

  // hold-to-draw (alternative flow; port of browser holdDraw.js)

  void CanvasWidget::setHoldDrawDelay(int ms) {
    holdDelayMs_ = std::max(100, std::min(3000, ms));
    hold_.setHoldDelay(holdDelayMs_);
  }

  double CanvasWidget::holdNowMs() const {
    return static_cast<double>(holdClock_.elapsed());
  }

  void CanvasWidget::beginHold(const QPoint& widgetPos) {
    holdPressPos_ = widgetPos;
    hold_.pointerDown(widgetPos.x(), widgetPos.y(), holdNowMs());
    holdTimer_.start();
  }

  void CanvasWidget::stopHold() {
    holdTimer_.stop();
    hold_.cancel();
    if (holdHasPreview_) {
      holdHasPreview_ = false;
      update();
    }
  }

  void CanvasWidget::handleHoldTick() {
    const core::HoldEvent ev = hold_.tick(holdNowMs());
    if (ev.action == core::HoldAction::START) holdStart(ev.x, ev.y);
    else if (ev.action == core::HoldAction::DROP) holdDrop(ev.x, ev.y);
  }

  // Hold completed → auto-enter drawing and seed the stroke. The target under the
  // press decides: existing point → continue that line; line body → insert a point
  // there then continue; empty → fresh line.
  void CanvasWidget::holdStart(double widgetX, double widgetY) {
    const core::Point ip{widgetX / scale_, widgetY / scale_};
    const core::HoldTarget t = core::holdDrawTarget(lines_, ip.x, ip.y);
    holdPrepend_ = false;
    if (t.kind == core::HoldTargetKind::CONTINUE_POINT) {
      selectedLineIdx_ = t.lineIdx;
      selectedPoint_ = t.ptIdx;
      startDrawingMode();
      // Holding the FIRST point extends the line backward: prepend new points
      // before it (index 0) instead of inserting after it as the second point.
      if (t.ptIdx == 0) { holdPrepend_ = true; continueInsertIdx_ = 0; }
    } else if (t.kind == core::HoldTargetKind::INSERT_SEGMENT) {
      insertPointOnSegment(t.lineIdx, t.ptIdx2, ip.x, ip.y);
      startDrawingMode();
    } else {
      selectedLineIdx_ = -1;
      selectedPoint_ = -1;
      startDrawingMode();
      currentLine_.points.push_back(ip);
      flyInPoint(-1, currentLine_, 0);
    }
    holdPreview_ = ip;
    holdHasPreview_ = true;
    update();
    emit changed();
    emit selectionChanged();
  }

  // Insert ip into the continued line at the clamped insert cursor + select it. Prepend
  // mode keeps inserting at the head; forward mode advances so points keep appending.
  void CanvasWidget::insertContinuationPoint(const core::Point& ip, bool advance) {
    core::Line& line = lines_[continueLineIdx_];
    const int at = std::max(
        0, std::min(continueInsertIdx_, static_cast<int>(line.points.size())));
    line.points.insert(line.points.begin() + at, ip);
    flyInPoint(continueLineIdx_, line, at);
    selectedPoint_ = at;
    if (advance) continueInsertIdx_ = at + 1;
  }

  // Dwell completed → drop a point (extends the in-progress / continued line).
  void CanvasWidget::holdDrop(double widgetX, double widgetY) {
    const core::Point ip{widgetX / scale_, widgetY / scale_};
    // Resting on the stroke's FIRST point closes it, exactly as clicking there does. The
    // shape is committed, so the gesture is over: end it rather than dropping more points
    // into a stroke that no longer exists.
    if (tryCloseShapeAt(ip)) {
      stopHold();
      emit selectionChanged();
      return;
    }
    if (continueLineIdx_ >= 0 &&
        continueLineIdx_ < static_cast<int>(lines_.size())) {
      insertContinuationPoint(ip, /*advance=*/!holdPrepend_);
    } else {
      currentLine_.points.push_back(ip);
      flyInPoint(-1, currentLine_, static_cast<int>(currentLine_.points.size()) - 1);
    }
    holdPreview_ = ip;
    holdHasPreview_ = true;
    update();
    emit changed();
    emit selectionChanged();
  }

  // Release after a hold stroke → commit the line and disable drawing again.
  void CanvasWidget::holdCommit() {
    holdHasPreview_ = false;
    holdPrepend_ = false;
    if (isDrawing_) stopDrawingMode();  // commits + emits drawingModeChanged(false)
    update();
  }

  // Origin of the hold-draw preview line: the tail of the in-progress line, or the
  // current insertion tail of the line being extended. nullptr = nothing to anchor.
  const core::Point* CanvasWidget::holdAnchor() const {
    if (!currentLine_.points.empty()) return &currentLine_.points.back();
    if (continueLineIdx_ >= 0 &&
        continueLineIdx_ < static_cast<int>(lines_.size())) {
      const std::vector<core::Point>& pts = lines_[continueLineIdx_].points;
      if (pts.empty()) return nullptr;
      // Prepend: the next point connects to the current head (continueInsertIdx_);
      // forward: it connects to the point just before the insertion tail.
      const int idx = holdPrepend_ ? continueInsertIdx_ : continueInsertIdx_ - 1;
      if (idx >= 0 && idx < static_cast<int>(pts.size())) return &pts[idx];
      return &pts.back();
    }
    return nullptr;
  }

}  // namespace stencil::gui

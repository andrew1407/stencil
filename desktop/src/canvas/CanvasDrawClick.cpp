#include "CanvasWidget.hpp"
#include "CanvasWidget.hpp"
#include "hitTest.hpp"

// A click while drawing: closing the shape, or adding the next point.

namespace stencil::gui {

  // Would a point placed at `ip` close the stroke being drawn? If so, close it into a
  // locked area and report it. The one close route: the click path and hold-to-draw both
  // come here. Browser twin: drawingApp.js tryCloseShapeAt.
  bool CanvasWidget::tryCloseShapeAt(const core::Point& ip) {
    if (!isDrawing_) return false;
    if (continueLineIdx_ >= 0 && continueLineIdx_ < static_cast<int>(lines_.size())) {
      core::Line& line = lines_[continueLineIdx_];
      if (!core::shouldCloseShape(line.points, ip, closeGrabSize(line))) return false;
      closeContinuedShape();
      emit changed();
      return true;
    }
    if (!core::shouldCloseShape(currentLine_.points, ip, closeGrabSize(currentLine_))) return false;
    currentLine_.points.push_back(currentLine_.points.front());
    currentLine_.locked = true;
    // Finishing a shape ends exactly like finishing an ordinary line: the stroke is
    // committed and NOTHING is selected — no selected-line bar pops up over the picture
    // you just drew (browser drawingApp.js #closeShape).
    stopDrawingMode();
    emit changed();
    return true;
  }

  void CanvasWidget::handleDrawingClick(const core::Point& ip,
                                        Qt::KeyboardModifiers mods,
                                        const QPoint& widgetPos) {
    // rect-draw press (browser pointerController.js startPan). Picking the rect tool is
    // the intent, so the press turns drawing on itself — it has no hold-to-draw flow to
    // fall back on (that one refuses in rect mode).
    if (drawMode_ == DrawMode::RECT && mods == Qt::NoModifier) {
      if (!isDrawing_) startDrawingMode();
      if (!isDrawing_) return;   // declined (no image / read-only) — nothing to sweep
      rectDrawActive_ = true;
      rectDrawStart_ = rectDrawEnd_ = widgetPos;
      update();
      return;
    }

    // when not drawing, a left-click hit-tests + selects a committed line
    // (port of drawingApp.js click-select ~1270) instead of being a no-op.
    if (!isDrawing_) {
      selectLineAt(ip.x, ip.y);
      return;
    }

    // in rect mode, areas are created by dragging, never click-to-add
    // (browser drawingApp.js ~1182).
    if (drawMode_ == DrawMode::RECT) return;

    // Continuation drawing: extend the line being continued (drawingApp.js canvasClick
    // continuation branch ~1201). A click on the stroke's first point closes it into a
    // locked area — whichever stroke is being drawn, continued or fresh.
    if (tryCloseShapeAt(ip)) return;

    if (continueLineIdx_ >= 0 &&
        continueLineIdx_ < static_cast<int>(lines_.size())) {
      const int idx = continueLineIdx_;
      insertContinuationPoint(ip, /*advance=*/true);
      update(lineRect(idx));
      emit changed();
      emit selectionChanged();
      return;
    }

    currentLine_.points.push_back(ip);
    flyInPoint(-1, currentLine_, static_cast<int>(currentLine_.points.size()) - 1);
    selectedPoint_ = static_cast<int>(currentLine_.points.size()) - 1;
    update();
    emit changed();
    emit selectionChanged();
  }

}  // namespace stencil::gui

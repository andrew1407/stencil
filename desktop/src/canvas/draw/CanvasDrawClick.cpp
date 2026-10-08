#include "CanvasWidget.hpp"
#include "hitTest.hpp"
#include "../../support/uiTimings.hpp"

// A click while drawing: closing the shape, or adding the next point.

namespace stencil::gui {

  // The one close route, click and hold-to-draw both (browser line/shapeBuilder.js): the first point
  // is the target, so while points are hidden nothing closes and the click lands as a point.
  bool CanvasWidget::tryCloseShapeAt(const core::Point& ip) {
    if (!isDrawing || !showPoints) return false;
    if (continueLineIdx >= 0 && continueLineIdx < static_cast<int>(lines.size())) {
      core::Line& line = lines[continueLineIdx];
      if (!core::shouldCloseShape(line.points, ip, closeGrabSize(line))) return false;
      closeContinuedShape();
      emit changed();
      return true;
    }
    if (!core::shouldCloseShape(currentLine.points, ip, closeGrabSize(currentLine))) return false;
    currentLine.points.push_back(currentLine.points.front());
    currentLine.locked = true;
    // Finishing a shape ends like finishing an ordinary line: committed, NOTHING selected
    // (browser drawingApp.js #closeShape).
    stopDrawingMode();
    emit changed();
    return true;
  }

  void CanvasWidget::handleDrawingClick(const core::Point& ip,
                                        Qt::KeyboardModifiers mods,
                                        const QPoint& widgetPos) {
    // rect-draw press (browser controller.js startPan). Picking the rect tool is the intent,
    // so the press turns drawing on itself - it has no hold-to-draw flow to fall back on.
    if (drawMode == DrawMode::RECT && mods == Qt::NoModifier) {
      if (!isDrawing) startDrawingMode();
      if (!isDrawing) return;   // declined (no image / read-only) — nothing to sweep
      gesture.kind = Gesture::RECT_DRAW;
      gesture.rectStart = gesture.rectEnd = widgetPos;
      update();
      return;
    }

    // when not drawing, a left-click hit-tests + selects a committed line
    // (port of drawingApp.js click-select ~1270) instead of being a no-op.
    if (!isDrawing) {
      selectLineAt(ip.x, ip.y);
      return;
    }

    // in rect mode, areas are created by dragging, never click-to-add
    // (browser drawingApp.js ~1182).
    if (drawMode == DrawMode::RECT) return;
    if (mods & Qt::ControlModifier) {
      breakChain(ip, /*repeat=*/false);
      return;
    }
    strokeFx.release(fxNow());   // this click is not the second of a double-click
    // A dropped point's segment waits out the double-click window, so a double-click never shows one.
    const double holdMs = support::uiTimings().doubleClickMs;

    // Continuation drawing (drawingApp.js canvasClick continuation branch): a click on the stroke's
    // first point closes it into a locked area, whichever stroke is being drawn.
    if (tryCloseShapeAt(ip)) return;

    if (continueLineIdx >= 0 &&
        continueLineIdx < static_cast<int>(lines.size())) {
      const int idx = continueLineIdx;
      insertContinuationPoint(ip, /*advance=*/true);
      gesture.dropLine = idx;
      gesture.dropIdx = continueInsertIdx - 1;
      flyInPoint(idx, lines[idx], gesture.dropIdx, nullptr, holdMs);
      update(lineRect(idx));
      emit changed();
      emit selectionChanged();
      return;
    }

    currentLine.points.push_back(ip);
    selectedPoint = static_cast<int>(currentLine.points.size()) - 1;
    flyInPoint(-1, currentLine, selectedPoint, nullptr, holdMs);
    gesture.dropLine = -1;
    gesture.dropIdx = selectedPoint;
    update();
    emit changed();
    emit selectionChanged();
  }

}  // namespace stencil::gui

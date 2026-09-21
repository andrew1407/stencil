#include "CanvasWidget.hpp"
#include "CanvasWidget.hpp"
#include "hitTest.hpp"

// A click while drawing: closing the shape, or adding the next point.

namespace stencil::gui {

  // The one close route: the click path and hold-to-draw both come here.
  // Browser twin: drawingApp.js tryCloseShapeAt.
  bool CanvasWidget::tryCloseShapeAt(const core::Point& ip) {
    if (!isDrawing) return false;
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
    // rect-draw press (browser pointerController.js startPan). Picking the rect tool is the intent,
    // so the press turns drawing on itself - it has no hold-to-draw flow to fall back on.
    if (drawMode == DrawMode::RECT && mods == Qt::NoModifier) {
      if (!isDrawing) startDrawingMode();
      if (!isDrawing) return;   // declined (no image / read-only) — nothing to sweep
      rectDrawActive = true;
      rectDrawStart = rectDrawEnd = widgetPos;
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

    // Continuation drawing (drawingApp.js canvasClick continuation branch): a click on the stroke's
    // first point closes it into a locked area, whichever stroke is being drawn.
    if (tryCloseShapeAt(ip)) return;

    if (continueLineIdx >= 0 &&
        continueLineIdx < static_cast<int>(lines.size())) {
      const int idx = continueLineIdx;
      insertContinuationPoint(ip, /*advance=*/true);
      update(lineRect(idx));
      emit changed();
      emit selectionChanged();
      return;
    }

    currentLine.points.push_back(ip);
    flyInPoint(-1, currentLine, static_cast<int>(currentLine.points.size()) - 1);
    selectedPoint = static_cast<int>(currentLine.points.size()) - 1;
    update();
    emit changed();
    emit selectionChanged();
  }

}  // namespace stencil::gui

#include "CanvasWidget.hpp"

// Rect drawing, and the widget → image-space mapping.

namespace stencil::gui {

  // Port of browser drawingApp.js createRect (standalone branch): a locked 4-corner rectangle
  // (drawPolygon closes the loop, so no 5th point).
  void CanvasWidget::createRect(double x1, double y1, double x2, double y2) {
    const double xa = std::min(x1, x2);
    const double xb = std::max(x1, x2);
    const double ya = std::min(y1, y2);
    const double yb = std::max(y1, y2);

    // Continuation drawing: append the 4 corners to the line being extended
    // instead of making a standalone area (drawingApp.js createRect ~1402).
    if (continueLineIdx >= 0 &&
        continueLineIdx < static_cast<int>(lines.size())) {
      core::Line& line = lines[continueLineIdx];
      const int at = std::max(
          0, std::min(continueInsertIdx, static_cast<int>(line.points.size())));
      const core::Point corners[4] = {
          {xa, ya}, {xb, ya}, {xb, yb}, {xa, yb}};
      line.points.insert(line.points.begin() + at, corners, corners + 4);
      flyInPoints(continueLineIdx, line, at, 4);
      continueInsertIdx = at + 4;
      selectedPoint = continueInsertIdx - 1;
      commitHistory();
      update();
      emit selectionChanged();
      return;
    }

    core::Line rect;
    rect.points = {{xa, ya}, {xb, ya}, {xb, yb}, {xa, yb}};  // exactly 4 corners
    rect.color = defColor.toStdString();
    rect.pointColor =
        (defPointColor.isEmpty() ? defColor : defPointColor).toStdString();
    rect.thickness = defThickness;
    rect.pointSize = defPointSize;
    rect.style = defStyle.toStdString();
    rect.locked = true;
    rect.fillColor = "transparent";

    lines.push_back(rect);
    selectedLineIdx = static_cast<int>(lines.size()) - 1;
    // The rect draws itself out of its first corner, edge by edge.
    flyInPoints(selectedLineIdx, lines.back(), 0, 4);
    selectedPoint = -1;
    commitHistory();
    update();
    emit selectionChanged();
  }

  core::Point CanvasWidget::toImageSpace(int widgetX, int widgetY) const {
    return {widgetX / scale, widgetY / scale};
  }

}  // namespace stencil::gui

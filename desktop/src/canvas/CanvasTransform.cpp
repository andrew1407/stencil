#include "CanvasWidget.hpp"
#include "CanvasWidget.hpp"
#include "hitTest.hpp"
#include "pointMath.hpp"

// Transforming a selection, and growing a line a point at a time.

namespace stencil::gui {

  // Alt+wheel: bump the thickness of the line under the cursor by +-1 (clamped 1-20).
  // Port of drawingApp.js #adjustThicknessAtCursor.
  void CanvasWidget::adjustThicknessAtCursor(double imageX, double imageY,
                                             int dir) {
    int lineIdx = -1;
    if (auto pt = core::findNearestPoint(lines, imageX, imageY, hitRadius(12.0))) {
      lineIdx = pt->lineIdx;
    } else {
      lineIdx = core::findLineAt(lines, imageX, imageY, hitRadius(8.0));
    }
    if (lineIdx < 0 || lineIdx >= static_cast<int>(lines.size())) return;

    core::Line& line = lines[lineIdx];
    const double newT = std::max(1.0, std::min(20.0, line.thickness + dir));
    if (newT == line.thickness) return;
    line.thickness = newT;
    update();
    emit selectionChanged();  // refresh the panel if this line is selected
    scheduleEditCommit();
  }

  // Apply `op` about the pivot: >=2 selected -> combined bbox centre; 1 -> the focused point, else
  // that line's bbox centre. Shared scaffold so rotate and flip cannot drift (no new core op).
  void CanvasWidget::transformSelection(
      const std::function<void(std::vector<core::Point>&, double, double)>& op) {
    if (compareReadOnly()) return;   // read-only compare view
    const auto sel = selectedIndices();
    if (sel.size() >= 2) {
      std::vector<core::Point> all;
      for (int i : sel)
        for (const auto& p : lines[i].points) all.push_back(p);
      if (all.size() < 2) return;
      const core::Point c = core::boundingBoxCenter(all);
      for (int i : sel) op(lines[i].points, c.x, c.y);
    } else {
      core::Line* line = selectedLine();
      if (!line || line->points.size() < 2) return;
      double cx;
      double cy;
      if (selectedPoint >= 0 &&
          selectedPoint < static_cast<int>(line->points.size())) {
        cx = line->points[selectedPoint].x;
        cy = line->points[selectedPoint].y;
      } else {
        const core::Point c = core::boundingBoxCenter(line->points);
        cx = c.x;
        cy = c.y;
      }
      op(line->points, cx, cy);
    }
    update();
    emit selectionChanged();
    scheduleEditCommit();
  }

  // Ctrl+Shift+wheel / Alt+R+←/→: rotate the selected line(s) about the selection pivot.
  // Port of #rotateSelectedLine (~1834).
  void CanvasWidget::rotateSelectedLine(double angleRad) {
    transformSelection([angleRad](std::vector<core::Point>& pts, double cx, double cy) {
      core::rotatePoints(pts, cx, cy, angleRad);
    });
  }

  // Alt+Shift+↑/↓: mirror the selected line(s) about the selection pivot (horizontal == left↔right,
  // x' = 2*cx - x; otherwise top↔bottom, y' = 2*cy - y) — core::flipPoints in place of the rotate.
  void CanvasWidget::flipSelectedLine(bool horizontal) {
    transformSelection([horizontal](std::vector<core::Point>& pts, double cx, double cy) {
      core::flipPoints(pts, horizontal, cx, cy);
    });
  }

  // Arrow-key nudge in image-space px (mirror of browser drawingApp.js nudgeSelected). Debounced
  // commit, like the wheel rotate, so a burst of key-repeats collapses into one undo step.
  void CanvasWidget::nudgeSelected(double dx, double dy) {
    if (compareReadOnly()) return;   // read-only compare view
    if (dx == 0.0 && dy == 0.0) return;
    const auto sel = selectedIndices();
    if (sel.empty()) return;
    for (int i : sel) {
      if (i < 0 || i >= static_cast<int>(lines.size())) continue;
      for (auto& p : lines[i].points) { p.x += dx; p.y += dy; }
    }
    update();
    emit selectionChanged();
    scheduleEditCommit();
  }

  // (Re)start the debounce so a burst of wheel ticks collapses into one undo
  // step. The single-shot timer fires commitHistory() once the wheel goes quiet.
  void CanvasWidget::scheduleEditCommit() { editCommitTimer.start(); }

  // Insert a point into an existing line between two of its points, select that line and focus
  // the new point. Port of drawingApp.js #insertPointOnSegment.
  void CanvasWidget::insertPointOnSegment(int lineIdx, int insertIdx, double x,
                                          double y) {
    if (lineIdx < 0 || lineIdx >= static_cast<int>(lines.size())) return;
    core::Line& line = lines[lineIdx];
    const int at = std::max(
        0, std::min(insertIdx, static_cast<int>(line.points.size())));
    line.points.insert(line.points.begin() + at, core::Point{x, y});
    // An inserted vertex comes out of the segment it split — from its own foot on the
    // old straight line, so the bend grows rather than appearing.
    if (at > 0 && at + 1 < static_cast<int>(line.points.size())) {
      const QPointF src = stroke::foot(line.points[at - 1], line.points[at + 1], x, y);
      flyInPoint(lineIdx, line, at, &src);
    } else {
      flyInPoint(lineIdx, line, at);
    }
    selectedLineIdx = lineIdx;
    selectedPoint = at;
    commitHistory();  // emits changed()
    update();
    emit selectionChanged();
  }

  // Add a point after the focused point (else at the line's tail), or start a new single-point
  // line when nothing is selected. Port of drawingApp.js #addConnectedPoint.
  void CanvasWidget::addConnectedPoint(double x, double y) {
    if (selectedLineIdx >= 0 &&
        selectedLineIdx < static_cast<int>(lines.size())) {
      core::Line& line = lines[selectedLineIdx];
      const int insertIdx = (selectedPoint >= 0)
                                ? selectedPoint + 1
                                : static_cast<int>(line.points.size());
      const int at = std::max(
          0, std::min(insertIdx, static_cast<int>(line.points.size())));
      line.points.insert(line.points.begin() + at, core::Point{x, y});
      flyInPoint(selectedLineIdx, line, at);
      selectedPoint = at;
      commitHistory();
      update();
      emit selectionChanged();
      return;
    }
    core::Line nl;
    nl.points = {{x, y}};
    nl.color = defColor.toStdString();
    nl.pointColor =
        (defPointColor.isEmpty() ? defColor : defPointColor).toStdString();
    nl.thickness = defThickness;
    nl.pointSize = defPointSize;
    nl.style = defStyle.toStdString();
    lines.push_back(nl);
    selectedLineIdx = static_cast<int>(lines.size()) - 1;
    flyInPoint(selectedLineIdx, lines.back(), 0);
    selectedPoint = 0;
    commitHistory();
    update();
    emit selectionChanged();
  }

  // Close the line being extended into a locked area, keep it selected, and leave
  // drawing mode. Port of drawingApp.js #closeContinuedShape (~1310).
  void CanvasWidget::closeContinuedShape() {
    const int li = continueLineIdx;
    if (li < 0 || li >= static_cast<int>(lines.size())) return;
    core::Line& line = lines[li];
    if (line.points.size() < 3) return;
    line.points.push_back(line.points.front());
    line.locked = true;
    if (line.fillColor.empty()) line.fillColor = "transparent";
    continueLineIdx = -1;
    continueInsertIdx = -1;
    currentLine = core::Line{};
    applyDefaultsToCurrent();
    isDrawing = false;
    selectedLineIdx = li;
    selectedPoint = -1;
    commitHistory();
    update();
    emit drawingModeChanged(false);
    emit selectionChanged();
  }

}  // namespace stencil::gui

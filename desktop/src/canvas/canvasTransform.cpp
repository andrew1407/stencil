#include "canvasWidget.hpp"
#include "canvasWidget.hpp"
#include "geometry.hpp"

// Transforming a selection, and growing a line a point at a time.

namespace stencil::gui {

  // Alt+wheel: bump the thickness of the line under the cursor by ±1 (clamped
  // 1–20). Prefers the hovered point's line, else the segment under the cursor.
  // Port of drawingApp.js #adjustThicknessAtCursor (~1810).
  void CanvasWidget::adjustThicknessAtCursor(double imageX, double imageY,
                                             int dir) {
    int lineIdx = -1;
    if (auto pt = core::findNearestPoint(lines_, imageX, imageY, hitRadius(12.0))) {
      lineIdx = pt->lineIdx;
    } else {
      lineIdx = core::findLineAt(lines_, imageX, imageY, hitRadius(8.0));
    }
    if (lineIdx < 0 || lineIdx >= static_cast<int>(lines_.size())) return;

    core::Line& line = lines_[lineIdx];
    const double newT = std::max(1.0, std::min(20.0, line.thickness + dir));
    if (newT == line.thickness) return;
    line.thickness = newT;
    update();
    emit selectionChanged();  // refresh the panel if this line is selected
    scheduleEditCommit();
  }

  // Apply `op` (rotate/flip) to the selection about its pivot: ≥2 selected → the
  // combined bbox centre; 1 → the focused point, else that line's bbox centre.
  // Shared scaffold so rotate and flip can't drift (no new core op).
  void CanvasWidget::transformSelection(
      const std::function<void(std::vector<core::Point>&, double, double)>& op) {
    if (compareReadOnly()) return;   // read-only compare view
    const auto sel = selectedIndices();
    if (sel.size() >= 2) {
      std::vector<core::Point> all;
      for (int i : sel)
        for (const auto& p : lines_[i].points) all.push_back(p);
      if (all.size() < 2) return;
      const core::Point c = core::boundingBoxCenter(all);
      for (int i : sel) op(lines_[i].points, c.x, c.y);
    } else {
      core::Line* line = selectedLine();
      if (!line || line->points.size() < 2) return;
      double cx;
      double cy;
      if (selectedPoint_ >= 0 &&
          selectedPoint_ < static_cast<int>(line->points.size())) {
        cx = line->points[selectedPoint_].x;
        cy = line->points[selectedPoint_].y;
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

  // Translate every selected line by (dx, dy) image-space px — the arrow-key nudge (mirror of
  // the browser drawingApp.js nudgeSelected). Debounced commit, like the wheel rotate, so a
  // burst of key-repeats collapses into one undo step.
  void CanvasWidget::nudgeSelected(double dx, double dy) {
    if (compareReadOnly()) return;   // read-only compare view
    if (dx == 0.0 && dy == 0.0) return;
    const auto sel = selectedIndices();
    if (sel.empty()) return;
    for (int i : sel) {
      if (i < 0 || i >= static_cast<int>(lines_.size())) continue;
      for (auto& p : lines_[i].points) { p.x += dx; p.y += dy; }
    }
    update();
    emit selectionChanged();
    scheduleEditCommit();
  }

  // (Re)start the debounce so a burst of wheel ticks collapses into one undo
  // step. The single-shot timer fires commitHistory() once the wheel goes quiet.
  void CanvasWidget::scheduleEditCommit() { editCommitTimer_.start(); }

  // Insert a new point into an existing line between two of its points, select
  // that line and focus the new point. Port of drawingApp.js #insertPointOnSegment
  // (~1335).
  void CanvasWidget::insertPointOnSegment(int lineIdx, int insertIdx, double x,
                                          double y) {
    if (lineIdx < 0 || lineIdx >= static_cast<int>(lines_.size())) return;
    core::Line& line = lines_[lineIdx];
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
    selectedLineIdx_ = lineIdx;
    selectedPoint_ = at;
    commitHistory();  // emits changed()
    update();
    emit selectionChanged();
  }

  // Add a point connected to the current selection (after the focused point, else
  // at the line's tail), or start a new single-point line when nothing is
  // selected. Port of drawingApp.js #addConnectedPoint (~1352).
  void CanvasWidget::addConnectedPoint(double x, double y) {
    if (selectedLineIdx_ >= 0 &&
        selectedLineIdx_ < static_cast<int>(lines_.size())) {
      core::Line& line = lines_[selectedLineIdx_];
      const int insertIdx = (selectedPoint_ >= 0)
                                ? selectedPoint_ + 1
                                : static_cast<int>(line.points.size());
      const int at = std::max(
          0, std::min(insertIdx, static_cast<int>(line.points.size())));
      line.points.insert(line.points.begin() + at, core::Point{x, y});
      flyInPoint(selectedLineIdx_, line, at);
      selectedPoint_ = at;
      commitHistory();
      update();
      emit selectionChanged();
      return;
    }
    core::Line nl;
    nl.points = {{x, y}};
    nl.color = defColor_.toStdString();
    nl.pointColor =
        (defPointColor_.isEmpty() ? defColor_ : defPointColor_).toStdString();
    nl.thickness = defThickness_;
    nl.pointSize = defPointSize_;
    nl.style = defStyle_.toStdString();
    lines_.push_back(nl);
    selectedLineIdx_ = static_cast<int>(lines_.size()) - 1;
    flyInPoint(selectedLineIdx_, lines_.back(), 0);
    selectedPoint_ = 0;
    commitHistory();
    update();
    emit selectionChanged();
  }

  // Close the line being extended into a locked area, keep it selected, and leave
  // drawing mode. Port of drawingApp.js #closeContinuedShape (~1310).
  void CanvasWidget::closeContinuedShape() {
    const int li = continueLineIdx_;
    if (li < 0 || li >= static_cast<int>(lines_.size())) return;
    core::Line& line = lines_[li];
    if (line.points.size() < 3) return;
    line.points.push_back(line.points.front());
    line.locked = true;
    if (line.fillColor.empty()) line.fillColor = "transparent";
    continueLineIdx_ = -1;
    continueInsertIdx_ = -1;
    currentLine_ = core::Line{};
    applyDefaultsToCurrent();
    isDrawing_ = false;
    selectedLineIdx_ = li;
    selectedPoint_ = -1;
    commitHistory();
    update();
    emit drawingModeChanged(false);
    emit selectionChanged();
  }

}  // namespace stencil::gui

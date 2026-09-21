#include "CanvasWidget.hpp"
#include "CanvasWidget.hpp"
#include "hitTest.hpp"

// Selecting points and lines, and the panel's index-keyed view of them.

namespace stencil::gui {

  // Precedence: an explicitly selected committed line, else the in-progress line while drawing,
  // else the last committed one. The mutable forwarder const_casts; never recurse.
  core::Line* CanvasWidget::mutablePanelLine() {
    return const_cast<core::Line*>(
        static_cast<const CanvasWidget*>(this)->panelLine());
  }

  const core::Line* CanvasWidget::panelLine() const {
    if (selectedLineIdx >= 0 &&
        selectedLineIdx < static_cast<int>(lines.size())) {
      return &lines[selectedLineIdx];
    }
    if (!currentLine.points.empty()) return &currentLine;
    if (!lines.empty()) return &lines.back();
    return nullptr;
  }

  void CanvasWidget::selectPoint(int index) {
    selectedPoint = index;
    update();
  }

  void CanvasWidget::deletePoint(int index) {
    if (compareReadOnly()) return;   // read-only compare view
    core::Line* line = mutablePanelLine();
    if (!line || index < 0 ||
        index >= static_cast<int>(line->points.size())) {
      return;
    }
    const bool committed = (line != &currentLine);
    line->points.erase(line->points.begin() + index);
    if (committed) {
      // Erase the line the panel actually shows (it need not be lines.back()).
      if (line->points.empty())
        lines.erase(lines.begin() + (line - lines.data()));
      selectedLineIdx = -1;  // index may now be stale/invalid
      commitHistory();
    }
    resetStrokeFx();
    clearHoverCache();   // indices shifted
    selectedPoint = -1;
    update();
    emit changed();
    emit selectionChanged();
  }

  void CanvasWidget::setPointCoord(int index, int axis, double value) {
    if (compareReadOnly()) return;   // read-only compare view
    core::Line* line = mutablePanelLine();
    if (!line || index < 0 || index >= static_cast<int>(line->points.size())) return;
    if (!std::isfinite(value)) return;
    if (axis == 0) line->points[index].x = value;
    else line->points[index].y = value;
    if (line != &currentLine) commitHistory();  // committed line → undoable edit
    update();
    emit changed();
    emit selectionChanged();  // refresh the panel's coord display (cm column, length)
  }

  void CanvasWidget::deselect() {
    selectedPoint = -1;
    selectedLineIdx = -1;
    continueLineIdx = continueInsertIdx = -1;
    update();
    emit selectionChanged();
  }

  // port of browser drawingApp.js setDrawMode ~1120. Guard equality so we
  // don't churn repaints / re-emit when nothing actually changed.
  void CanvasWidget::setDrawMode(DrawMode mode) {
    if (drawMode == mode) return;
    drawMode = mode;
    emit drawModeChanged(drawMode);
  }

  // Hit-test committed lines, topmost wins (drawingApp.js): a point hit selects its line AND
  // focuses that point (the rotation pivot); a segment hit selects with no focused point.
  int CanvasWidget::selectLineAt(double x, double y) {
    selectedLines.clear();   // a plain click leaves multi-select mode
    if (auto pt = core::findNearestPoint(lines, x, y, hitRadius(12.0))) {
      selectedLineIdx = pt->lineIdx;
      selectedPoint = pt->ptIdx;
    } else {
      selectedLineIdx = core::findLineAt(lines, x, y, hitRadius(8.0));
      selectedPoint = -1;
    }
    update();
    emit selectionChanged();
    return selectedLineIdx;
  }

  std::vector<int> CanvasWidget::selectedIndices() const {
    std::vector<int> out;
    const int n = static_cast<int>(lines.size());
    if (!selectedLines.empty()) {
      for (int i : selectedLines)
        if (i >= 0 && i < n) out.push_back(i);
    } else if (selectedLineIdx >= 0 && selectedLineIdx < n) {
      out.push_back(selectedLineIdx);
    }
    return out;
  }

  bool CanvasWidget::isLineSelected(int i) const {
    if (!selectedLines.empty())
      return std::find(selectedLines.begin(), selectedLines.end(), i) != selectedLines.end();
    return i == selectedLineIdx;
  }

  // Toggle line `idx` in/out of the multi-select set (Ctrl+Shift+click). Shared by the
  // point-hit and Lines-tab-row entry points.
  void CanvasWidget::toggleLineIndex(int idx) {
    if (idx < 0 || idx >= static_cast<int>(lines.size())) return;
    // Seed the set from the current single selection on the first Ctrl+Shift+click.
    if (selectedLines.empty() && selectedLineIdx >= 0 && selectedLineIdx != idx)
      selectedLines.push_back(selectedLineIdx);
    auto it = std::find(selectedLines.begin(), selectedLines.end(), idx);
    if (it != selectedLines.end()) selectedLines.erase(it);
    else selectedLines.push_back(idx);
    if (selectedLines.size() == 1) {
      selectedLineIdx = selectedLines.front();  // back to single-select (its editor returns)
      selectedLines.clear();
    } else {
      selectedLineIdx = -1;  // 0 or 2+ selected → no single-line editor
    }
    selectedPoint = -1;
    update();
    emit selectionChanged();
  }

  void CanvasWidget::toggleLineSelection(const core::Point& ip) {
    int idx = -1;
    if (auto pt = core::findNearestPoint(lines, ip.x, ip.y, hitRadius(12.0))) idx = pt->lineIdx;
    else idx = core::findLineAt(lines, ip.x, ip.y, hitRadius(8.0));
    toggleLineIndex(idx);  // idx == -1 (empty space) is a no-op inside
  }

  // Single-select line `idx` from the Lines-tab list. Mirrors selectLineAt's post-hit
  // state (clears multi-select, no focused point). Port of drawingApp.js selectLineFromList.
  void CanvasWidget::selectLineByIndex(int idx) {
    if (idx < 0 || idx >= static_cast<int>(lines.size())) return;
    selectedLines.clear();
    selectedLineIdx = idx;
    selectedPoint = -1;
    update();
    emit selectionChanged();
  }

  // Index-keyed twin of toggleLineSelection(point): Ctrl+Shift+click a Lines-tab row.
  void CanvasWidget::toggleLineSelectionByIndex(int idx) { toggleLineIndex(idx); }

  // Remove committed line `idx` (Lines-tab 🗑). Keeps the single + multi selection and the
  // focused point consistent with the now-shifted indices. Port of drawingApp.js removeLine.
  void CanvasWidget::removeLineByIndex(int idx) {
    if (compareReadOnly()) return;   // read-only compare view
    resetStrokeFx();
    if (idx < 0 || idx >= static_cast<int>(lines.size())) return;
    lines.erase(lines.begin() + idx);
    clearHoverCache();   // indices shifted
    for (auto it = selectedLines.begin(); it != selectedLines.end();) {
      if (*it == idx) { it = selectedLines.erase(it); continue; }
      if (*it > idx) --*it;
      ++it;
    }
    if (selectedLineIdx == idx) { selectedLineIdx = -1; selectedPoint = -1; }
    else if (selectedLineIdx > idx) --selectedLineIdx;
    // A removal that leaves exactly one multi-selected line drops back to single-select.
    if (selectedLines.size() == 1) { selectedLineIdx = selectedLines.front(); selectedLines.clear(); }
    commitHistory();
    update();
    emit changed();
    emit selectionChanged();
  }

  // Mutable forwarder: const_cast the const overload's result (renderToImage
  // pattern); never recurse through the mutable version.
  core::Line* CanvasWidget::selectedLine() {
    return const_cast<core::Line*>(
        static_cast<const CanvasWidget*>(this)->selectedLine());
  }

  const core::Line* CanvasWidget::selectedLine() const {
    if (selectedLineIdx < 0 ||
        selectedLineIdx >= static_cast<int>(lines.size())) {
      return nullptr;
    }
    return &lines[selectedLineIdx];
  }

}  // namespace stencil::gui

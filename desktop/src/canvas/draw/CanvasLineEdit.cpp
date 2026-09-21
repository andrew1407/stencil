#include "CanvasWidget.hpp"
#include "CanvasWidget.hpp"
#include "hitTest.hpp"

// Editing the selected line, and the hover cache the panels read.

namespace stencil::gui {

  // selected-line mutators + delete (port of applySelectionChange ~1674
  // and canvasDblClick delete ~1515)

  void CanvasWidget::mutateSelectedLine(const std::function<void(core::Line&)>& set, bool commit) {
    if (compareReadOnly()) return;   // read-only compare view (selection-panel edits)
    core::Line* line = selectedLine();
    if (!line) return;
    set(*line);
    // A live preview rides the same debounce the wheel edits use, so the whole gesture collapses
    // into ONE undo step once it goes quiet.
    if (commit) commitHistory();
    else scheduleEditCommit();
    update();
    emit selectionChanged();
  }

  void CanvasWidget::setSelectedLineColor(const QString& color, bool preview) {
    mutateSelectedLine([&](core::Line& line) {
      // Recolouring the stroke must never recolour the points: a line still on the
      // inherit fallback ('' pointColor) pins its rendered colour first (browser parity).
      if (line.pointColor.empty()) line.pointColor = line.color;
      line.color = color.toStdString();
    }, !preview);
  }

  void CanvasWidget::setSelectedLineThickness(double thickness) {
    mutateSelectedLine([&](core::Line& line) { line.thickness = thickness; });
  }

  void CanvasWidget::setSelectedLinePointSize(double pointSize) {
    mutateSelectedLine([&](core::Line& line) { line.pointSize = pointSize; });
  }

  void CanvasWidget::setSelectedLinePointColor(const QString& pointColor, bool preview) {
    mutateSelectedLine(
        [&](core::Line& line) { line.pointColor = pointColor.toStdString(); }, !preview);
  }

  void CanvasWidget::setSelectedLineStyle(const QString& style) {
    mutateSelectedLine([&](core::Line& line) { line.style = style.toStdString(); });
  }

  void CanvasWidget::setSelectedLineFill(const QString& fillColor, bool preview) {
    mutateSelectedLine([&](core::Line& line) { line.fillColor = fillColor.toStdString(); }, !preview);
  }

  // Deletes the WHOLE selection (browser parity: removeSelectedLines): erase from the highest
  // index down so lower indices stay valid, and commit ONE history entry.
  void CanvasWidget::deleteSelectedLine() {
    if (compareReadOnly()) return;   // read-only compare view
    std::vector<int> sel = selectedIndices();
    if (sel.empty()) return;
    std::sort(sel.begin(), sel.end(), std::greater<int>());
    for (int idx : sel) {
      if (idx < 0 || idx >= static_cast<int>(lines.size())) continue;
      lines.erase(lines.begin() + idx);
    }
    resetStrokeFx();
    clearHoverCache();   // indices shifted
    selectedLines.clear();
    selectedLineIdx = -1;
    selectedPoint = -1;
    commitHistory();
    update();
    emit changed();
    emit selectionChanged();
  }

  // interactive editing helpers (port of drawingApp.js)

  // True when the hovered point changed, so the caller repaints. In-progress line first
  // (lineIdx -1), then committed. Port of #findNearestPointWithIdx + the hoverPt bookkeeping.
  bool CanvasWidget::updateHover(double imageX, double imageY) {
    int li = -1;
    int pi = -1;
    if (auto idx = core::nearestPointInLine(currentLine.points, imageX, imageY,
                                            hitRadius(12.0))) {
      li = -1;
      pi = *idx;
    }
    if (pi < 0) {
      if (auto pt = core::findNearestPoint(lines, imageX, imageY, hitRadius(12.0))) {
        li = pt->lineIdx;
        pi = pt->ptIdx;
      }
    }
    // The committed LINE under the cursor (a point hit names its line, else a stroke
    // hit) — tints the panel's Lines-list row, the reverse of setListHoverLine.
    const int over =
        (li >= 0) ? li : core::findLineAt(lines, imageX, imageY, hitRadius(8.0));
    if (li == hoverLineIdx && pi == hoverPointIdx && over == hoverOverLineIdx)
      return false;
    hoverLineIdx = li;
    hoverPointIdx = pi;
    hoverOverLineIdx = over;
    emit canvasHoverChanged(li, pi, over);
    return true;
  }

  int CanvasWidget::panelLineIdx() const {
    const core::Line* l = panelLine();
    if (!l || l == &currentLine || lines.empty()) return -1;
    return static_cast<int>(l - lines.data());
  }

  void CanvasWidget::setListHoverPoint(int ptIdx) {
    const core::Line* l = panelLine();
    const int n = l ? static_cast<int>(l->points.size()) : 0;
    const int i = (ptIdx >= 0 && ptIdx < n) ? ptIdx : -1;
    if (i == listHoverPointIdx) return;
    listHoverPointIdx = i;
    update();
  }

  void CanvasWidget::setListHoverLine(int lineIdx) {
    const int i =
        (lineIdx >= 0 && lineIdx < static_cast<int>(lines.size())) ? lineIdx : -1;
    if (i == listHoverLineIdx) return;
    listHoverLineIdx = i;
    update();
  }

  void CanvasWidget::clearHoverCache() {
    if (hoverLineIdx == -1 && hoverPointIdx == -1 && hoverOverLineIdx == -1 &&
        listHoverPointIdx == -1 && listHoverLineIdx == -1) {
      return;
    }
    hoverLineIdx = -1;
    hoverPointIdx = -1;
    hoverOverLineIdx = -1;
    listHoverPointIdx = -1;
    listHoverLineIdx = -1;
    emit canvasHoverChanged(-1, -1, -1);
  }

}  // namespace stencil::gui

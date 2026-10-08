#include "CanvasWidget.hpp"
#include "../../support/control/lineLimits.hpp"

#include <algorithm>

// Editing the selected line, and the hover cache the panels read.

namespace stencil::gui {

  // selected-line mutators + delete (port of applySelectionChange ~1674
  // and canvasDblClick delete ~1515)

  // A Lines-tab row names its own line by `idx` and leaves the selection as it is.
  void CanvasWidget::mutateSelectedLine(const std::function<void(core::Line&)>& set, bool commit, int idx) {
    if (compareReadOnly()) return;   // read-only compare view (selection-panel edits)
    core::Line* line = idx < 0 ? selectedLine() : idx < static_cast<int>(lines.size()) ? &lines[idx] : nullptr;
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

  // Held to LIMITS, as the browser's applyLineChange holds them.
  void CanvasWidget::setSelectedLineThickness(double thickness, int idx) {
    const support::lineLimits::Table& limits = support::lineLimits::table();
    const double t = std::clamp(thickness, double(limits.thickMin), double(limits.thickMax));
    mutateSelectedLine([&](core::Line& line) { line.thickness = t; }, true, idx);
  }

  void CanvasWidget::setSelectedLinePointSize(double pointSize, int idx) {
    const support::lineLimits::Table& limits = support::lineLimits::table();
    const double s = std::clamp(pointSize, double(limits.pointMin), double(limits.pointMax));
    mutateSelectedLine([&](core::Line& line) { line.pointSize = s; }, true, idx);
  }

  // '' = no colour of the points' own: they draw in the line's (core::pointColorOr).
  void CanvasWidget::setSelectedLinePointColor(const QString& pointColor, bool preview, int idx) {
    mutateSelectedLine(
        [&](core::Line& line) { line.pointColor = pointColor.toStdString(); }, !preview, idx);
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
    if (auto idx = model::pointIn(currentLine.points, shownMarks(), imageX, imageY, grabHitRadius())) {
      li = -1;
      pi = *idx;
    }
    if (pi < 0) {
      if (auto pt = model::pointAt(lines, shownMarks(), imageX, imageY, grabHitRadius())) {
        li = pt->lineIdx;
        pi = pt->ptIdx;
      }
    }
    // The committed LINE under the cursor (a point hit names its line, else a stroke
    // hit) — tints the panel's Lines-list row, the reverse of setListHoverLine.
    const int over =
        (li >= 0) ? li : model::lineAt(lines, shownMarks(), imageX, imageY, lineHitRadius());
    if (li == hover.lineIdx && pi == hover.pointIdx && over == hover.overLineIdx)
      return false;
    hover.lineIdx = li;
    hover.pointIdx = pi;
    hover.overLineIdx = over;
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
    if (i == hover.listPointIdx) return;
    hover.listPointIdx = i;
    update();
  }

  void CanvasWidget::setListHoverLine(int lineIdx) {
    const int i =
        (lineIdx >= 0 && lineIdx < static_cast<int>(lines.size())) ? lineIdx : -1;
    if (i == hover.listLineIdx) return;
    hover.listLineIdx = i;
    update();
  }

  void CanvasWidget::clearHoverCache() {
    if (hover.lineIdx == -1 && hover.pointIdx == -1 && hover.overLineIdx == -1 &&
        hover.listPointIdx == -1 && hover.listLineIdx == -1) {
      return;
    }
    hover.lineIdx = -1;
    hover.pointIdx = -1;
    hover.overLineIdx = -1;
    hover.listPointIdx = -1;
    hover.listLineIdx = -1;
    emit canvasHoverChanged(-1, -1, -1);
  }

}  // namespace stencil::gui

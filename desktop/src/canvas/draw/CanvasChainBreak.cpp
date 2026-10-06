#include "CanvasWidget.hpp"

#include <cmath>

// Breaking the chain while drawing (⌘/Ctrl+click, double-click): the stroke so far is kept — a
// lone point too — and an unconnected stroke opens at the press. Browser twin: core/draw/chainBreak.js.

namespace stencil::gui {

  // `repeat`: a double-click's second press — the point its first press dropped moves to the new chain.
  bool CanvasWidget::breakChain(core::Point ip, bool repeat) {
    if (repeat) {
      const int li = gesture.dropLine, pi = gesture.dropIdx;
      gesture.dropIdx = -1;
      core::Line* line = li == -1 ? &currentLine
                         : (li >= 0 && li < static_cast<int>(lines.size()) ? &lines[li] : nullptr);
      if (!line || pi < 0 || pi >= static_cast<int>(line->points.size())) return false;
      const core::Point dropped = line->points[pi];
      if (std::hypot(dropped.x - ip.x, dropped.y - ip.y) > grabHitRadius()) return false;
      line->points.erase(line->points.begin() + pi);
      if (li == continueLineIdx && pi < continueInsertIdx) --continueInsertIdx;
      ip = dropped;
    }
    resetStrokeFx();
    clearHoverCache();
    const bool continuing = continueLineIdx >= 0 && continueLineIdx < static_cast<int>(lines.size());
    const bool kept = continuing || !currentLine.points.empty();
    if (!continuing && kept) lines.push_back(currentLine);
    continueLineIdx = continueInsertIdx = -1;
    currentLine = core::Line{};
    applyDefaultsToCurrent();
    if (kept) commitHistory();
    selectedLineIdx = -1;
    currentLine.points.push_back(ip);
    flyInPoint(-1, currentLine, 0);
    selectedPoint = 0;
    gesture.dropLine = -1;
    gesture.dropIdx = 0;
    update();
    emit changed();
    emit selectionChanged();
    return true;
  }

}  // namespace stencil::gui

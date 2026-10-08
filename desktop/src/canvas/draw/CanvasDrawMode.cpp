#include "CanvasWidget.hpp"

// Entering and leaving drawing mode, plus clear/undo/redo.

namespace stencil::gui {

  // Port of drawingApp.js startDrawingMode (~1070): begin a fresh in-progress
  // line and arm the click gate. Requires a loaded image.
  void CanvasWidget::startDrawingMode() {
    if (compareReadOnly()) return;   // read-only compare view
    if (image.isNull() || isDrawing) return;
    isDrawing = true;

    // Continuation: a committed line is selected -> extend it, from its tail or the focused point.
    // Port of drawingApp.js startDrawingMode continuation branch.
    if (selectedLineIdx >= 0 &&
        selectedLineIdx < static_cast<int>(lines.size())) {
      continueLineIdx = selectedLineIdx;
      const core::Line& line = lines[continueLineIdx];
      continueInsertIdx = (selectedPoint >= 0)
                               ? selectedPoint + 1
                               : static_cast<int>(line.points.size());
      currentLine = core::Line{};  // unused while continuing
      update();
      emit drawingModeChanged(true);
      emit selectionChanged();
      return;
    }

    continueLineIdx = -1;
    continueInsertIdx = -1;
    currentLine = core::Line{};
    applyDefaultsToCurrent();
    selectedPoint = -1;
    update();
    emit drawingModeChanged(true);
    emit selectionChanged();
  }

  // Port of drawingApp.js stopDrawingMode (~1138): commit the in-progress line
  // (when it has >= 2 points) and disarm the gate.
  void CanvasWidget::stopDrawingMode() {
    if (!isDrawing) return;

    // Continuation: the extended line is already in lines — just commit & reset,
    // keeping it selected (drawingApp.js stopDrawingMode continuation ~1140).
    if (continueLineIdx >= 0) {
      const int li = continueLineIdx;
      continueLineIdx = -1;
      continueInsertIdx = -1;
      currentLine = core::Line{};
      applyDefaultsToCurrent();
      isDrawing = false;
      selectedLineIdx = (li < static_cast<int>(lines.size())) ? li : -1;
      commitHistory();
      update();
      emit drawingModeChanged(false);
      emit selectionChanged();
      return;
    }

    if (currentLine.points.size() >= 2) {
      lines.push_back(currentLine);
      // A vertex still in the air keeps flying on the line the stroke just became.
      strokeFx.rekey(-1, static_cast<int>(lines.size()) - 1);
      commitHistory();
    }
    currentLine = core::Line{};
    applyDefaultsToCurrent();
    isDrawing = false;
    selectedPoint = -1;
    selectedLineIdx = -1;
    continueLineIdx = continueInsertIdx = -1;
    update();
    emit drawingModeChanged(false);
    emit selectionChanged();
  }

  void CanvasWidget::startNewLine() {
    if (compareReadOnly()) return;   // read-only compare view
    resetStrokeFx();
    if (currentLine.points.size() >= 2) {
      lines.push_back(currentLine);
      commitHistory();
    }
    currentLine = core::Line{};
    applyDefaultsToCurrent();
    selectedPoint = -1;
    update();
    emit selectionChanged();
  }

  void CanvasWidget::deleteLastPoint() {
    if (compareReadOnly()) return;   // read-only compare view
    if (currentLine.points.empty()) return;
    currentLine.points.pop_back();
    clearHoverCache();   // the hovered in-progress point may be the one removed
    selectedPoint = -1;
    update();
    emit selectionChanged();
  }

  void CanvasWidget::clearAll() {
    if (compareReadOnly()) return;   // read-only compare view
    resetStrokeFx();
    if (lines.empty() && currentLine.points.empty()) return;
    lines.clear();
    clearHoverCache();
    currentLine = core::Line{};
    applyDefaultsToCurrent();
    selectedPoint = -1;
    selectedLineIdx = -1;
    selectedLines.clear();
    continueLineIdx = continueInsertIdx = -1;
    commitHistory();
    update();
    emit selectionChanged();
  }

  void CanvasWidget::commitHistory() {
    pushStep();
    emit changed();
  }

  void CanvasWidget::takeHistoryStep(const std::optional<core::EditorMemento>& step) {
    if (!step) return;
    const QString wasMode = imageFilter, wasTint = filterColor.name();
    const bool refit = restoreMemento(*step);
    clearHoverCache();   // the snapshot may not contain the hovered indices
    keepLineSelection();
    if (refit) {
      continueLineIdx = continueInsertIdx = -1;
      setFixedSize(QSize(qRound(image.width() * scale), qRound(image.height() * scale)));
    }
    update();
    // Copies: the window's applier rewrites the scene's filter while it runs.
    const QString nowMode = imageFilter;
    const QColor nowTint = filterColor;
    if (nowMode != wasMode || nowTint.name() != wasTint) emit filterRestored(nowMode, nowTint);
    emit changed();
    emit selectionChanged();
    if (refit) emit fitRequested();
  }

  void CanvasWidget::undo() {
    if (compareReadOnly()) return;   // read-only compare view
    resetStrokeFx();
    takeHistoryStep(history.undo());
  }

  void CanvasWidget::redo() {
    if (compareReadOnly()) return;   // read-only compare view
    resetStrokeFx();
    takeHistoryStep(history.redo());
  }

}  // namespace stencil::gui

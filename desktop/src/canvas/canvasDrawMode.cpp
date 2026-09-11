#include "canvasWidget.hpp"
#include "canvasWidget.hpp"

// Entering and leaving drawing mode, plus clear/undo/redo.

namespace stencil::gui {

  // Port of drawingApp.js startDrawingMode (~1070): begin a fresh in-progress
  // line and arm the click gate. Requires a loaded image.
  void CanvasWidget::startDrawingMode() {
    if (compareReadOnly()) return;   // read-only compare view
    if (image_.isNull() || isDrawing_) return;
    isDrawing_ = true;

    // Continuation: a committed line is selected -> extend it. New points connect
    // to its tail, or to the focused point if one is selected. Port of
    // drawingApp.js startDrawingMode continuation branch (~1079).
    if (selectedLineIdx_ >= 0 &&
        selectedLineIdx_ < static_cast<int>(lines_.size())) {
      continueLineIdx_ = selectedLineIdx_;
      const core::Line& line = lines_[continueLineIdx_];
      continueInsertIdx_ = (selectedPoint_ >= 0)
                               ? selectedPoint_ + 1
                               : static_cast<int>(line.points.size());
      currentLine_ = core::Line{};  // unused while continuing
      update();
      emit drawingModeChanged(true);
      emit selectionChanged();
      return;
    }

    continueLineIdx_ = -1;
    continueInsertIdx_ = -1;
    currentLine_ = core::Line{};
    applyDefaultsToCurrent();
    selectedPoint_ = -1;
    update();
    emit drawingModeChanged(true);
    emit selectionChanged();
  }

  // Port of drawingApp.js stopDrawingMode (~1138): commit the in-progress line
  // (when it has >= 2 points) and disarm the gate.
  void CanvasWidget::stopDrawingMode() {
    if (!isDrawing_) return;

    // Continuation: the extended line is already in lines_ — just commit & reset,
    // keeping it selected (drawingApp.js stopDrawingMode continuation ~1140).
    if (continueLineIdx_ >= 0) {
      const int li = continueLineIdx_;
      continueLineIdx_ = -1;
      continueInsertIdx_ = -1;
      currentLine_ = core::Line{};
      applyDefaultsToCurrent();
      isDrawing_ = false;
      selectedLineIdx_ = (li < static_cast<int>(lines_.size())) ? li : -1;
      commitHistory();
      update();
      emit drawingModeChanged(false);
      emit selectionChanged();
      return;
    }

    if (currentLine_.points.size() >= 2) {
      lines_.push_back(currentLine_);
      // A vertex still in the air keeps flying on the line the stroke just became.
      strokeFx_.rekey(-1, static_cast<int>(lines_.size()) - 1);
      commitHistory();
    }
    currentLine_ = core::Line{};
    applyDefaultsToCurrent();
    isDrawing_ = false;
    selectedPoint_ = -1;
    selectedLineIdx_ = -1;
    continueLineIdx_ = continueInsertIdx_ = -1;
    update();
    emit drawingModeChanged(false);
    emit selectionChanged();
  }

  void CanvasWidget::startNewLine() {
    if (compareReadOnly()) return;   // read-only compare view
    resetStrokeFx();
    if (currentLine_.points.size() >= 2) {
      lines_.push_back(currentLine_);
      commitHistory();
    }
    currentLine_ = core::Line{};
    applyDefaultsToCurrent();
    selectedPoint_ = -1;
    update();
    emit selectionChanged();
  }

  void CanvasWidget::deleteLastPoint() {
    if (compareReadOnly()) return;   // read-only compare view
    if (currentLine_.points.empty()) return;
    currentLine_.points.pop_back();
    clearHoverCache();   // the hovered in-progress point may be the one removed
    selectedPoint_ = -1;
    update();
    emit selectionChanged();
  }

  void CanvasWidget::clearAll() {
    if (compareReadOnly()) return;   // read-only compare view
    resetStrokeFx();
    if (lines_.empty() && currentLine_.points.empty()) return;
    lines_.clear();
    clearHoverCache();
    currentLine_ = core::Line{};
    applyDefaultsToCurrent();
    selectedPoint_ = -1;
    selectedLineIdx_ = -1;
    continueLineIdx_ = continueInsertIdx_ = -1;
    commitHistory();
    update();
    emit selectionChanged();
  }

  void CanvasWidget::commitHistory() {
    history_.push(lines_);
    emit changed();
  }

  void CanvasWidget::undo() {
    if (compareReadOnly()) return;   // read-only compare view
    resetStrokeFx();
    if (auto snap = history_.undo()) {
      lines_ = *snap;
      clearHoverCache();   // the snapshot may not contain the hovered indices
      currentLine_ = core::Line{};
      applyDefaultsToCurrent();
      selectedPoint_ = -1;
      selectedLineIdx_ = -1;
      update();
      emit changed();
      emit selectionChanged();
    }
  }

  void CanvasWidget::redo() {
    if (compareReadOnly()) return;   // read-only compare view
    resetStrokeFx();
    if (auto snap = history_.redo()) {
      lines_ = *snap;
      clearHoverCache();   // see undo()
      currentLine_ = core::Line{};
      applyDefaultsToCurrent();
      selectedPoint_ = -1;
      selectedLineIdx_ = -1;
      update();
      emit changed();
      emit selectionChanged();
    }
  }

}  // namespace stencil::gui

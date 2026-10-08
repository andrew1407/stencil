#include "CanvasWidget.hpp"

// The lines the canvas holds (a peer's with their crop and turn), a committed filter pick and the
// zoom it shows them at; the look they are drawn with is the scene's (scene/CanvasScene.cpp).

namespace stencil::gui {

  void CanvasWidget::setLines(const core::Lines& lines) {
    commitLines(lines);
    resetSteps();   // a fresh document: the stack starts here
  }

  // A scripted or planned edit lands here instead: the user's undo stack survives and the
  // change becomes one step on it, where setLines drops the stack on the floor.
  void CanvasWidget::commitLines(const core::Lines& lines) {
    resetStrokeFx();
    this->lines = lines;
    clearHoverCache();   // indices are meaningless against the new set
    currentLine = core::Line{};
    applyDefaultsToCurrent();
    selectedPoint = -1;
    selectedLineIdx = -1;
    continueLineIdx = continueInsertIdx = -1;
    commitHistory();   // pushes the snapshot and emits changed()
    update();
    emit selectionChanged();
  }

  bool CanvasWidget::commitLayout(const core::Lines& lines, const core::CropRect& crop, int quarters, int mirrored) {
    resetStrokeFx();
    const bool rebuilt = CanvasScene::commitLayout(lines, crop, quarters, mirrored);
    clearHoverCache();
    selectedPoint = -1;
    selectedLineIdx = -1;
    continueLineIdx = continueInsertIdx = -1;
    if (rebuilt) setFixedSize(QSize(qRound(image.width() * scale), qRound(image.height() * scale)));
    update();
    emit changed();
    emit selectionChanged();
    if (rebuilt) emit fitRequested();
    return rebuilt;
  }

  bool CanvasWidget::commitFilter(const QString& mode, const QColor& tint) {
    const bool stepped = CanvasScene::commitFilter(mode, tint);
    if (stepped) emit changed();
    return stepped;
  }

  void CanvasWidget::setScale(double scale) {
    this->scale = scale;
    if (!image.isNull()) {
      setFixedSize(QSize(qRound(image.width() * this->scale),
                         qRound(image.height() * this->scale)));
    }
    update();
  }

}  // namespace stencil::gui

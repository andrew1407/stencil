#include "CanvasWidget.hpp"

// Loading, rotating, flipping and cropping the picture this canvas shows: the scene's edit, then the view
// resized to it, the selection dropped and the owner told.

namespace stencil::gui {

  bool CanvasWidget::loadImage(const QString& path, const QImage& decoded) {
    if (decoded.isNull()) return false;
    ++pictureGen;
    originalImage = decoded;
    imagePath = path;
    rotationQuarters = 0;
    mirrored = false;
    // Auto-crop from the center to the page aspect (cut the surplus sides). The
    // original is kept; image shows only this region.
    cropRect = defaultCropRect();
    rebuildCroppedFromOriginal();
    lines.clear();
    clearHoverCache();
    currentLine = core::Line{};
    applyDefaultsToCurrent();
    selectedPoint = -1;
    selectedLineIdx = -1;
    continueLineIdx = continueInsertIdx = -1;
    filterDirty = true;    // new image -> rebuild filter cache
    resetSteps();
    setFixedSize(QSize(qRound(image.width() * scale),
                       qRound(image.height() * scale)));
    update();
    emit changed();
    emit selectionChanged();
    return true;
  }

  void CanvasWidget::rotateImage(bool clockwise) {
    if (originalImage.isNull()) return;
    CanvasScene::rotateImage(clockwise);
    selectedPoint = -1;
    selectedLineIdx = -1;
    continueLineIdx = continueInsertIdx = -1;
    setFixedSize(QSize(qRound(image.width() * scale),
                       qRound(image.height() * scale)));
    update();
    emit changed();
    emit selectionChanged();
  }

  void CanvasWidget::flipImage() {
    if (originalImage.isNull()) return;
    CanvasScene::flipImage();
    selectedPoint = -1;
    selectedLineIdx = -1;
    continueLineIdx = continueInsertIdx = -1;
    update();
    emit changed();
    emit selectionChanged();
  }

  void CanvasWidget::applyCrop(const core::CropRect& rect, bool recalc) {
    if (originalImage.isNull()) return;
    CanvasScene::applyCrop(rect, recalc);
    selectedPoint = -1;
    selectedLineIdx = -1;
    continueLineIdx = continueInsertIdx = -1;
    setFixedSize(QSize(qRound(image.width() * scale),
                       qRound(image.height() * scale)));
    update();
    emit changed();
    emit selectionChanged();
  }

  void CanvasWidget::restore(const QString& path, const core::Lines& lines,
                             double scale, const core::CropRect& cropRect,
                             int rotationQuarters, const QImage& decoded, bool mirrored) {
    resetStrokeFx();
    this->scale = scale > 0 ? scale : 1.0;
    CanvasScene::restore(path, lines, cropRect, rotationQuarters, decoded, mirrored);
    clearHoverCache();
    selectedPoint = -1;
    selectedLineIdx = -1;
    continueLineIdx = continueInsertIdx = -1;
    if (!image.isNull()) {
      setFixedSize(QSize(qRound(image.width() * this->scale),
                         qRound(image.height() * this->scale)));
    }
    update();
    emit changed();
    emit selectionChanged();
  }

}  // namespace stencil::gui

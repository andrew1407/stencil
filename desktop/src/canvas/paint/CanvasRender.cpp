#include "CanvasWidget.hpp"
#include "../../support/motionPrefs.hpp"   // support::motionReduced()

// Adopting an in-memory picture, and leaving the canvas empty for the idle card.

namespace stencil::gui {

  // Adopt an in-memory image (clipboard paste / generated). Clears the file
  // path and resets lines/history/scale, mirroring a fresh load.
  void CanvasWidget::loadFromImage(const QImage& img, bool keepZoom) {
    if (img.isNull()) return;
    ++pictureGen;
    unsetCursor();   // drop the idle "pointing hand" (set while the empty-canvas hint was showing)
    blankPage = false;   // a fresh load is a picture until the owner marks it a blank
    originalImage = img.convertToFormat(QImage::Format_ARGB32);
    rotationQuarters = 0;
    cropRect = defaultCropRect();
    rebuildCroppedFromOriginal();
    imagePath.clear();
    lines.clear();
    currentLine = core::Line{};
    applyDefaultsToCurrent();
    selectedPoint = -1;
    selectedLineIdx = -1;
    continueLineIdx = continueInsertIdx = -1;
    if (!keepZoom) scale = 1.0;
    filterDirty = true;
    resetSteps();
    setFixedSize(QSize(qRound(image.width() * scale),
                       qRound(image.height() * scale)));
    update();
    emit changed();
    emit selectionChanged();
  }

  // Rotation is set before the crop is applied (the crop rect lives in rotated-original space);
  // a zero-width crop falls back to the default centered crop.
  void CanvasWidget::loadFromImage(const QImage& img, const core::CropRect& cropRect,
                                   int rotationQuarters) {
    if (img.isNull()) return;
    CanvasScene::loadFromImage(img, cropRect, rotationQuarters);
    selectedPoint = -1;
    selectedLineIdx = -1;
    continueLineIdx = continueInsertIdx = -1;
    scale = 1.0;
    setFixedSize(QSize(qRound(image.width() * scale),
                       qRound(image.height() * scale)));
    update();
    emit changed();
    emit selectionChanged();
  }

  QRect CanvasWidget::idleCardGlobalRect() const {
    if (!image.isNull() || idle.hidden || idle.cardRect().isEmpty()) return {};
    const QRect local = idle.cardRect().toRect();
    return QRect(mapToGlobal(local.topLeft()), local.size());
  }

  void CanvasWidget::setIdleHintHidden(bool on) {
    if (idle.hidden == on) return;
    idle.hidden = on;
    if (on) unsetCursor();
    update();
  }

  void CanvasWidget::clearImage() {
    // Arrives when a picture LEAVES, never on an already-empty editor (browser `idle-arriving`).
    if (!image.isNull() && !support::motionReduced()) idle.startArrival();
    resetStrokeFx();
    ++pictureGen;
    blankPage = false;
    originalImage = QImage();
    image = QImage();
    imagePath.clear();
    rotationQuarters = 0;
    cropRect = core::CropRect{};
    lines.clear();
    currentLine = core::Line{};
    applyDefaultsToCurrent();
    selectedPoint = -1;
    selectedLineIdx = -1;
    continueLineIdx = continueInsertIdx = -1;
    scale = 1.0;
    filterDirty = true;
    resetSteps();
    // Release the image-locked fixed size AND actually shrink back: the host scroll area is not
    // widgetResizable, so relaxing the constraints alone keeps the old image's size.
    setMinimumSize(320, 240);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    resize(minimumSize());
    updateGeometry();
    update();
    emit changed();
    emit selectionChanged();
  }

}  // namespace stencil::gui

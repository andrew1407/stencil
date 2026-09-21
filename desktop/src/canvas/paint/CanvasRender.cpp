#include "CanvasWidget.hpp"
#include "CanvasWidget.hpp"

#include <QFileInfo>

// Render-to-image, the image accessors and loadFromImage.

namespace stencil::gui {


  // Native-resolution render of an export variant. Mirrors the browser's export/service.js
  // renderExportCanvas / renderSplitExportCanvas op-for-op.
  QImage CanvasWidget::renderToImage(const QString& variant, bool withDivider) const {
    if (image.isNull()) return QImage();

    if (variant == "original") {
      // The cropped+rotated original alone — no filter, no annotations.
      return image.convertToFormat(QImage::Format_ARGB32);
    }

    // Resolve the filtered pixels at native size (const-safe: we don't touch the
    // cached filteredImage/filterDirty here, we recompute locally if needed).
    QImage base;
    if (imageFilter == "none") {
      base = image.convertToFormat(QImage::Format_ARGB32);
    } else if (!filterDirty && !filteredImage.isNull()) {
      base = filteredImage;  // cache already current
    } else {
      // Recompute via a temporary canvas to keep this method const.
      CanvasWidget* self = const_cast<CanvasWidget*>(this);
      self->rebuildFilteredImage();
      base = filteredImage.isNull()
                 ? image.convertToFormat(QImage::Format_ARGB32)
                 : filteredImage;
    }

    QImage out = base.convertToFormat(QImage::Format_ARGB32);
    if (variant == "tint") return out;  // filtered, no annotations

    // "current" and "split" draw the visible lines/points at native scale - no hover/selection rings
    // (highlight = false) - honouring the show flags (drawLineScaled checks them itself).
    {
      QPainter p(&out);
      p.setRenderHint(QPainter::Antialiasing, true);
      for (int i = 0; i < static_cast<int>(lines.size()); ++i)
        drawLineScaled(p, lines[i], i, 1.0, /*highlight=*/false, /*live=*/false);
      drawLineScaled(p, currentLine, -1, 1.0, /*highlight=*/false, /*live=*/false);
    }
    if (variant == "split") {
      QPainter p(&out);
      p.setRenderHint(QPainter::Antialiasing, true);
      const QString mode = compareMode == "horizontal" ? "horizontal" : "vertical";
      paintCompareSplit(p, mode, /*scale=*/1.0, withDivider);
    }
    return out;
  }

  QImage CanvasWidget::renderToImage(bool withOverlay) const {
    return renderToImage(withOverlay ? QStringLiteral("current") : QStringLiteral("tint"));
  }

  QString CanvasWidget::imageBaseName() const {
    if (imagePath.isEmpty()) return QStringLiteral("image");
    return QFileInfo(imagePath).completeBaseName();
  }

  QString CanvasWidget::imageExt() const {
    if (imagePath.isEmpty()) return QStringLiteral("png");
    const QString suffix = QFileInfo(imagePath).suffix();
    return suffix.isEmpty() ? QStringLiteral("png") : suffix;
  }

  // Adopt an in-memory image (clipboard paste / generated). Clears the file
  // path and resets lines/history/scale, mirroring a fresh load.
  void CanvasWidget::loadFromImage(const QImage& img, bool keepZoom) {
    if (img.isNull()) return;
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
    history.reset(lines);
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
    blankPage = false;   // a fresh load is a picture until the owner marks it a blank
    originalImage = img.convertToFormat(QImage::Format_ARGB32);
    this->rotationQuarters = ((rotationQuarters % 4) + 4) % 4;  // before defaultCropRect/rebuild
    this->cropRect = cropRect.width > 0 ? cropRect : defaultCropRect();
    rebuildCroppedFromOriginal();
    imagePath.clear();
    lines.clear();
    currentLine = core::Line{};
    applyDefaultsToCurrent();
    selectedPoint = -1;
    selectedLineIdx = -1;
    continueLineIdx = continueInsertIdx = -1;
    scale = 1.0;
    filterDirty = true;
    history.reset(lines);
    setFixedSize(QSize(qRound(image.width() * scale),
                       qRound(image.height() * scale)));
    update();
    emit changed();
    emit selectionChanged();
  }

  QRect CanvasWidget::idleCardGlobalRect() const {
    if (!image.isNull() || idleHintHidden || idleCardRect.isEmpty()) return {};
    const QRect local = idleCardRect.toRect();
    return QRect(mapToGlobal(local.topLeft()), local.size());
  }

  void CanvasWidget::setIdleHintHidden(bool on) {
    if (idleHintHidden == on) return;
    idleHintHidden = on;
    if (on) unsetCursor();
    update();
  }

  void CanvasWidget::clearImage() {
    resetStrokeFx();
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
    history.reset(lines);
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

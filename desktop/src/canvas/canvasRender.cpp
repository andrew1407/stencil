#include "canvasWidget.hpp"
#include "canvasWidget.hpp"

#include <QFileInfo>

// Render-to-image, the image accessors and loadFromImage.

namespace stencil::gui {


  // Native-resolution render of an export variant — see the header doc for what each
  // variant means. Mirrors the browser's exportService.js renderExportCanvas /
  // renderSplitExportCanvas op-for-op.
  QImage CanvasWidget::renderToImage(const QString& variant, bool withDivider) const {
    if (image_.isNull()) return QImage();

    if (variant == "original") {
      // The cropped+rotated original alone — no filter, no annotations.
      return image_.convertToFormat(QImage::Format_ARGB32);
    }

    // Resolve the filtered pixels at native size (const-safe: we don't touch the
    // cached filteredImage_/filterDirty_ here, we recompute locally if needed).
    QImage base;
    if (imageFilter_ == "none") {
      base = image_.convertToFormat(QImage::Format_ARGB32);
    } else if (!filterDirty_ && !filteredImage_.isNull()) {
      base = filteredImage_;  // cache already current
    } else {
      // Recompute via a temporary canvas to keep this method const.
      CanvasWidget* self = const_cast<CanvasWidget*>(this);
      self->rebuildFilteredImage();
      base = filteredImage_.isNull()
                 ? image_.convertToFormat(QImage::Format_ARGB32)
                 : filteredImage_;
    }

    QImage out = base.convertToFormat(QImage::Format_ARGB32);
    if (variant == "tint") return out;  // filtered, no annotations

    // "current" and "split" both draw the visible lines/points at native scale — no
    // hover/selection rings baked in (highlight = false) — honoring the show flags
    // (drawLineScaled checks showLines_/showPoints_ itself).
    {
      QPainter p(&out);
      p.setRenderHint(QPainter::Antialiasing, true);
      for (int i = 0; i < static_cast<int>(lines_.size()); ++i)
        drawLineScaled(p, lines_[i], i, 1.0, /*highlight=*/false, /*live=*/false);
      drawLineScaled(p, currentLine_, -1, 1.0, /*highlight=*/false, /*live=*/false);
    }
    if (variant == "split") {
      QPainter p(&out);
      p.setRenderHint(QPainter::Antialiasing, true);
      const QString mode = compareMode_ == "horizontal" ? "horizontal" : "vertical";
      paintCompareSplit(p, mode, /*scale=*/1.0, withDivider);
    }
    return out;
  }

  QImage CanvasWidget::renderToImage(bool withOverlay) const {
    return renderToImage(withOverlay ? QStringLiteral("current") : QStringLiteral("tint"));
  }

  QString CanvasWidget::imageBaseName() const {
    if (imagePath_.isEmpty()) return QStringLiteral("image");
    return QFileInfo(imagePath_).completeBaseName();
  }

  QString CanvasWidget::imageExt() const {
    if (imagePath_.isEmpty()) return QStringLiteral("png");
    const QString suffix = QFileInfo(imagePath_).suffix();
    return suffix.isEmpty() ? QStringLiteral("png") : suffix;
  }

  // Adopt an in-memory image (clipboard paste / generated). Clears the file
  // path and resets lines/history/scale, mirroring a fresh load.
  void CanvasWidget::loadFromImage(const QImage& img, bool keepZoom) {
    if (img.isNull()) return;
    unsetCursor();   // drop the idle "pointing hand" (set while the empty-canvas hint was showing)
    blankPage_ = false;   // a fresh load is a picture until the owner marks it a blank
    originalImage_ = img.convertToFormat(QImage::Format_ARGB32);
    rotationQuarters_ = 0;
    cropRect_ = defaultCropRect();
    rebuildCroppedFromOriginal();
    imagePath_.clear();
    lines_.clear();
    currentLine_ = core::Line{};
    applyDefaultsToCurrent();
    selectedPoint_ = -1;
    selectedLineIdx_ = -1;
    continueLineIdx_ = continueInsertIdx_ = -1;
    if (!keepZoom) scale_ = 1.0;
    filterDirty_ = true;
    history_.reset(lines_);
    setFixedSize(QSize(qRound(image_.width() * scale_),
                       qRound(image_.height() * scale_)));
    update();
    emit changed();
    emit selectionChanged();
  }

  // Adopt an in-memory image WITH a known geometry (a reopened server project).
  // Rotation is set before the crop is applied (the crop rect lives in rotated-
  // original space); a zero-width crop falls back to the default centered crop.
  void CanvasWidget::loadFromImage(const QImage& img, const core::CropRect& cropRect,
                                   int rotationQuarters) {
    if (img.isNull()) return;
    blankPage_ = false;   // a fresh load is a picture until the owner marks it a blank
    originalImage_ = img.convertToFormat(QImage::Format_ARGB32);
    rotationQuarters_ = ((rotationQuarters % 4) + 4) % 4;  // before defaultCropRect/rebuild
    cropRect_ = cropRect.width > 0 ? cropRect : defaultCropRect();
    rebuildCroppedFromOriginal();
    imagePath_.clear();
    lines_.clear();
    currentLine_ = core::Line{};
    applyDefaultsToCurrent();
    selectedPoint_ = -1;
    selectedLineIdx_ = -1;
    continueLineIdx_ = continueInsertIdx_ = -1;
    scale_ = 1.0;
    filterDirty_ = true;
    history_.reset(lines_);
    setFixedSize(QSize(qRound(image_.width() * scale_),
                       qRound(image_.height() * scale_)));
    update();
    emit changed();
    emit selectionChanged();
  }

  QRect CanvasWidget::idleCardGlobalRect() const {
    if (!image_.isNull() || idleHintHidden_ || idleCardRect_.isEmpty()) return {};
    const QRect local = idleCardRect_.toRect();
    return QRect(mapToGlobal(local.topLeft()), local.size());
  }

  void CanvasWidget::setIdleHintHidden(bool on) {
    if (idleHintHidden_ == on) return;
    idleHintHidden_ = on;
    if (on) unsetCursor();
    update();
  }

  void CanvasWidget::clearImage() {
    resetStrokeFx();
    blankPage_ = false;
    originalImage_ = QImage();
    image_ = QImage();
    imagePath_.clear();
    rotationQuarters_ = 0;
    cropRect_ = core::CropRect{};
    lines_.clear();
    currentLine_ = core::Line{};
    applyDefaultsToCurrent();
    selectedPoint_ = -1;
    selectedLineIdx_ = -1;
    continueLineIdx_ = continueInsertIdx_ = -1;
    scale_ = 1.0;
    filterDirty_ = true;
    history_.reset(lines_);
    // Release the image-locked fixed size AND actually shrink back: the host scroll
    // area is not widgetResizable, so relaxing the constraints alone keeps the old
    // image's size and the idle hint paints centred in a huge off-screen rect.
    setMinimumSize(320, 240);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    resize(minimumSize());
    updateGeometry();
    update();
    emit changed();
    emit selectionChanged();
  }

}  // namespace stencil::gui

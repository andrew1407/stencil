#include "CanvasWidget.hpp"
#include "CanvasWidget.hpp"

#include <QTransform>

// Loading, rotating and cropping the picture this canvas shows.

namespace stencil::gui {

  bool CanvasWidget::loadImage(const QString& path, const QImage& decoded) {
    QImage img = decoded;
    if (img.isNull() && !img.load(path)) return false;
    originalImage_ = img;
    imagePath_ = path;
    rotationQuarters_ = 0;
    // Auto-crop from the center to the page aspect (cut the surplus sides). The
    // original is kept; image_ shows only this region.
    cropRect_ = defaultCropRect();
    rebuildCroppedFromOriginal();
    lines_.clear();
    clearHoverCache();
    currentLine_ = core::Line{};
    applyDefaultsToCurrent();
    selectedPoint_ = -1;
    selectedLineIdx_ = -1;
    continueLineIdx_ = continueInsertIdx_ = -1;
    filterDirty_ = true;    // new image -> rebuild filter cache
    history_.reset(lines_);
    setFixedSize(QSize(qRound(image_.width() * scale_),
                       qRound(image_.height() * scale_)));
    update();
    emit changed();
    emit selectionChanged();
    return true;
  }

  // crop (shared cropGeometry; mirrors browser DrawingApp)
  void CanvasWidget::setPageCm(double widthCm, double heightCm) {
    if (widthCm > 0) pageWidthCm_ = widthCm;
    if (heightCm > 0) pageHeightCm_ = heightCm;
  }

  // The original with the current rotation baked in (clockwise quarter-turns).
  // Qt's QTransform::rotate(+90) is clockwise in the default y-down space, so the
  // quarter count maps straight through. Returns the original untouched at 0.
  QImage CanvasWidget::effectiveOriginalImage() const {
    const int q = ((rotationQuarters_ % 4) + 4) % 4;
    if (q == 0 || originalImage_.isNull()) return originalImage_;
    return originalImage_.transformed(QTransform().rotate(q * 90.0));
  }

  core::CropRect CanvasWidget::defaultCropRect() const {
    if (originalImage_.isNull()) return {};
    // The crop lives in the rotated image's space, so shape it to those dims.
    const QImage rot = effectiveOriginalImage();
    const double iw = rot.width();
    const double ih = rot.height();
    const double aspect =
        core::cropAspect(pageWidthCm_, pageHeightCm_, core::isAlbumOrientation(iw, ih));
    return core::centeredCrop(iw, ih, aspect);
  }

  void CanvasWidget::rebuildCroppedFromOriginal() {
    if (originalImage_.isNull()) {
      image_ = QImage();
      return;
    }
    const QImage rot = effectiveOriginalImage();
    if (cropRect_.width <= 0) {
      image_ = rot;
    } else {
      const QRect r(qRound(cropRect_.x), qRound(cropRect_.y),
                    qRound(cropRect_.width), qRound(cropRect_.height));
      image_ = rot.copy(r.intersected(rot.rect()));
    }
    filterDirty_ = true;
  }

  void CanvasWidget::rotateImage(bool clockwise) {
    if (originalImage_.isNull()) return;
    // The crop currently lives in the rotated-original space; capture those dims
    // before the turn so the rect transports correctly.
    const QImage rot = effectiveOriginalImage();
    const double iw = rot.width();
    const double ih = rot.height();
    // Points first — they rotate inside the OLD crop box (width x height).
    core::rotateLinePointsQuarter(lines_, cropRect_.width, cropRect_.height, clockwise);
    core::CropRect nr = core::rotateCropRectQuarter(cropRect_, iw, ih, clockwise);
    rotationQuarters_ = (((rotationQuarters_ + (clockwise ? 1 : -1)) % 4) + 4) % 4;
    // Snap to integer pixels within the freshly-rotated original.
    const QImage rot2 = effectiveOriginalImage();
    const double nw2 = rot2.width(), nh2 = rot2.height();
    nr.width = std::clamp(std::round(nr.width), 1.0, nw2);
    nr.height = std::clamp(std::round(nr.height), 1.0, nh2);
    nr.x = std::clamp(std::round(nr.x), 0.0, nw2 - nr.width);
    nr.y = std::clamp(std::round(nr.y), 0.0, nh2 - nr.height);
    cropRect_ = nr;
    rebuildCroppedFromOriginal();

    currentLine_ = core::Line{};
    applyDefaultsToCurrent();
    selectedPoint_ = -1;
    selectedLineIdx_ = -1;
    continueLineIdx_ = continueInsertIdx_ = -1;
    history_.reset(lines_);
    setFixedSize(QSize(qRound(image_.width() * scale_),
                       qRound(image_.height() * scale_)));
    update();
    emit changed();
    emit selectionChanged();
  }

  void CanvasWidget::applyCrop(const core::CropRect& rect, bool recalc) {
    if (originalImage_.isNull()) return;
    // Snap to integer pixels within the rotated original (the crop's pixel space).
    const QImage rot = effectiveOriginalImage();
    const double iw = rot.width();
    const double ih = rot.height();
    core::CropRect nr;
    nr.width = std::clamp(std::round(rect.width), 1.0, iw);
    nr.height = std::clamp(std::round(rect.height), 1.0, ih);
    nr.x = std::clamp(std::round(rect.x), 0.0, iw - nr.width);
    nr.y = std::clamp(std::round(rect.y), 0.0, ih - nr.height);

    if (recalc && cropRect_.width > 0) {
      const core::CropChange ch = core::cropChange(cropRect_, nr);
      if (ch.orientationChanged)
        lines_.clear();  // the caller confirms this with the user first
      else if (ch.scale != 1.0)
        core::scaleLinePoints(lines_, ch.scale);
    }
    cropRect_ = nr;
    rebuildCroppedFromOriginal();

    currentLine_ = core::Line{};
    applyDefaultsToCurrent();
    selectedPoint_ = -1;
    selectedLineIdx_ = -1;
    continueLineIdx_ = continueInsertIdx_ = -1;
    history_.reset(lines_);
    setFixedSize(QSize(qRound(image_.width() * scale_),
                       qRound(image_.height() * scale_)));
    update();
    emit changed();
    emit selectionChanged();
  }

  void CanvasWidget::restore(const QString& path, const core::Lines& lines,
                             double scale, const core::CropRect& cropRect,
                             int rotationQuarters, const QImage& decoded) {
    resetStrokeFx();
    scale_ = scale > 0 ? scale : 1.0;
    if (!path.isEmpty()) {
      QImage img = decoded;
      if (!img.isNull() || img.load(path)) {
        blankPage_ = false;   // the owner re-marks reopened blanks after restore
        originalImage_ = img;
        imagePath_ = path;
        // Rotation must be set before defaultCropRect / rebuild read it.
        rotationQuarters_ = ((rotationQuarters % 4) + 4) % 4;
        // Re-apply the stored crop, or default-crop sessions saved before
        // cropping existed (cropRect.width == 0).
        cropRect_ = cropRect.width > 0 ? cropRect : defaultCropRect();
        rebuildCroppedFromOriginal();
      }
    }
    lines_ = lines;
    clearHoverCache();
    currentLine_ = core::Line{};
    applyDefaultsToCurrent();
    selectedPoint_ = -1;
    selectedLineIdx_ = -1;
    continueLineIdx_ = continueInsertIdx_ = -1;
    filterDirty_ = true;
    history_.reset(lines_);
    if (!image_.isNull()) {
      setFixedSize(QSize(qRound(image_.width() * scale_),
                         qRound(image_.height() * scale_)));
    }
    update();
    emit changed();
    emit selectionChanged();
  }

}  // namespace stencil::gui

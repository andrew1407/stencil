#include "CanvasWidget.hpp"
#include "CanvasWidget.hpp"

#include <QTransform>

// Loading, rotating and cropping the picture this canvas shows.

namespace stencil::gui {

  bool CanvasWidget::loadImage(const QString& path, const QImage& decoded) {
    QImage img = decoded;
    if (img.isNull() && !img.load(path)) return false;
    originalImage = img;
    imagePath = path;
    rotationQuarters = 0;
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
    history.reset(lines);
    setFixedSize(QSize(qRound(image.width() * scale),
                       qRound(image.height() * scale)));
    update();
    emit changed();
    emit selectionChanged();
    return true;
  }

  // crop (shared cropGeometry; mirrors browser DrawingApp)
  void CanvasWidget::setPageCm(double widthCm, double heightCm) {
    if (widthCm > 0) pageWidthCm = widthCm;
    if (heightCm > 0) pageHeightCm = heightCm;
  }

  // Qt's QTransform::rotate(+90) is clockwise in the default y-down space, so the quarter count
  // maps straight through. Returns the original untouched at 0.
  QImage CanvasWidget::effectiveOriginalImage() const {
    const int q = ((rotationQuarters % 4) + 4) % 4;
    if (q == 0 || originalImage.isNull()) return originalImage;
    return originalImage.transformed(QTransform().rotate(q * 90.0));
  }

  core::CropRect CanvasWidget::defaultCropRect() const {
    if (originalImage.isNull()) return {};
    // The crop lives in the rotated image's space, so shape it to those dims.
    const QImage rot = effectiveOriginalImage();
    const double iw = rot.width();
    const double ih = rot.height();
    const double aspect =
        core::cropAspect(pageWidthCm, pageHeightCm, core::isAlbumOrientation(iw, ih));
    return core::centeredCrop(iw, ih, aspect);
  }

  void CanvasWidget::rebuildCroppedFromOriginal() {
    if (originalImage.isNull()) {
      image = QImage();
      return;
    }
    const QImage rot = effectiveOriginalImage();
    if (cropRect.width <= 0) {
      image = rot;
    } else {
      const QRect r(qRound(cropRect.x), qRound(cropRect.y),
                    qRound(cropRect.width), qRound(cropRect.height));
      image = rot.copy(r.intersected(rot.rect()));
    }
    filterDirty = true;
  }

  void CanvasWidget::rotateImage(bool clockwise) {
    if (originalImage.isNull()) return;
    // The crop currently lives in the rotated-original space; capture those dims
    // before the turn so the rect transports correctly.
    const QImage rot = effectiveOriginalImage();
    const double iw = rot.width();
    const double ih = rot.height();
    // Points first — they rotate inside the OLD crop box (width x height).
    core::rotateLinePointsQuarter(lines, cropRect.width, cropRect.height, clockwise);
    core::CropRect nr = core::rotateCropRectQuarter(cropRect, iw, ih, clockwise);
    rotationQuarters = (((rotationQuarters + (clockwise ? 1 : -1)) % 4) + 4) % 4;
    // Snap to integer pixels within the freshly-rotated original.
    const QImage rot2 = effectiveOriginalImage();
    const double nw2 = rot2.width(), nh2 = rot2.height();
    nr.width = std::clamp(std::round(nr.width), 1.0, nw2);
    nr.height = std::clamp(std::round(nr.height), 1.0, nh2);
    nr.x = std::clamp(std::round(nr.x), 0.0, nw2 - nr.width);
    nr.y = std::clamp(std::round(nr.y), 0.0, nh2 - nr.height);
    cropRect = nr;
    rebuildCroppedFromOriginal();

    currentLine = core::Line{};
    applyDefaultsToCurrent();
    selectedPoint = -1;
    selectedLineIdx = -1;
    continueLineIdx = continueInsertIdx = -1;
    history.reset(lines);
    setFixedSize(QSize(qRound(image.width() * scale),
                       qRound(image.height() * scale)));
    update();
    emit changed();
    emit selectionChanged();
  }

  void CanvasWidget::applyCrop(const core::CropRect& rect, bool recalc) {
    if (originalImage.isNull()) return;
    // Snap to integer pixels within the rotated original (the crop's pixel space).
    const QImage rot = effectiveOriginalImage();
    const double iw = rot.width();
    const double ih = rot.height();
    core::CropRect nr;
    nr.width = std::clamp(std::round(rect.width), 1.0, iw);
    nr.height = std::clamp(std::round(rect.height), 1.0, ih);
    nr.x = std::clamp(std::round(rect.x), 0.0, iw - nr.width);
    nr.y = std::clamp(std::round(rect.y), 0.0, ih - nr.height);

    if (recalc && cropRect.width > 0) {
      const core::CropChange ch = core::cropChange(cropRect, nr);
      if (ch.orientationChanged)
        lines.clear();  // the caller confirms this with the user first
      else if (ch.scale != 1.0)
        core::scaleLinePoints(lines, ch.scale);
    }
    cropRect = nr;
    rebuildCroppedFromOriginal();

    currentLine = core::Line{};
    applyDefaultsToCurrent();
    selectedPoint = -1;
    selectedLineIdx = -1;
    continueLineIdx = continueInsertIdx = -1;
    history.reset(lines);
    setFixedSize(QSize(qRound(image.width() * scale),
                       qRound(image.height() * scale)));
    update();
    emit changed();
    emit selectionChanged();
  }

  void CanvasWidget::restore(const QString& path, const core::Lines& lines,
                             double scale, const core::CropRect& cropRect,
                             int rotationQuarters, const QImage& decoded) {
    resetStrokeFx();
    this->scale = scale > 0 ? scale : 1.0;
    if (!path.isEmpty()) {
      QImage img = decoded;
      if (!img.isNull() || img.load(path)) {
        blankPage = false;   // the owner re-marks reopened blanks after restore
        originalImage = img;
        imagePath = path;
        // Rotation must be set before defaultCropRect / rebuild read it.
        this->rotationQuarters = ((rotationQuarters % 4) + 4) % 4;
        // Re-apply the stored crop, or default-crop sessions saved before
        // cropping existed (cropRect.width == 0).
        this->cropRect = cropRect.width > 0 ? cropRect : defaultCropRect();
        rebuildCroppedFromOriginal();
      }
    }
    this->lines = lines;
    clearHoverCache();
    currentLine = core::Line{};
    applyDefaultsToCurrent();
    selectedPoint = -1;
    selectedLineIdx = -1;
    continueLineIdx = continueInsertIdx = -1;
    filterDirty = true;
    history.reset(this->lines);
    if (!image.isNull()) {
      setFixedSize(QSize(qRound(image.width() * this->scale),
                         qRound(image.height() * this->scale)));
    }
    update();
    emit changed();
    emit selectionChanged();
  }

}  // namespace stencil::gui

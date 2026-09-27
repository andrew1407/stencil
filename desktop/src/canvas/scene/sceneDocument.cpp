#include "CanvasScene.hpp"
#include "imageTurn.hpp"

// The document's own edits — load, restore, crop, turn, the lines and their undo steps — with no
// view to resize and no one to tell; CanvasWidget's same-named edits wrap these.

namespace stencil::gui {

  core::CropRect CanvasScene::defaultCropRect() const {
    if (originalImage.isNull()) return {};
    // The crop lives in the rotated image's space, so shape it to those dims.
    const QSize rot = model::turnedSize(originalImage.size(), rotationQuarters);
    const double iw = rot.width();
    const double ih = rot.height();
    const double aspect =
        core::cropAspect(pageWidthCm, pageHeightCm, core::isAlbumOrientation(iw, ih));
    return core::centeredCrop(iw, ih, aspect);
  }

  void CanvasScene::rebuildCroppedFromOriginal() {
    if (originalImage.isNull()) {
      image = QImage();
      return;
    }
    if (cropRect.width <= 0) {
      image = model::turn(originalImage, rotationQuarters);
    } else {
      const QRect r(qRound(cropRect.x), qRound(cropRect.y),
                    qRound(cropRect.width), qRound(cropRect.height));
      image = model::turnAndCrop(originalImage, rotationQuarters, r);
    }
    filterDirty = true;
  }

  void CanvasScene::loadFromImage(const QImage& img, const core::CropRect& cropRect,
                                  int rotationQuarters) {
    if (img.isNull()) return;
    ++pictureGen;
    blankPage = false;   // a fresh load is a picture until the owner marks it a blank
    originalImage = img.convertToFormat(QImage::Format_ARGB32);
    this->rotationQuarters = ((rotationQuarters % 4) + 4) % 4;  // before defaultCropRect/rebuild
    this->cropRect = cropRect.width > 0 ? cropRect : defaultCropRect();
    rebuildCroppedFromOriginal();
    imagePath.clear();
    lines.clear();
    currentLine = core::Line{};
    applyDefaultsToCurrent();
    filterDirty = true;
    resetSteps();
  }

  void CanvasScene::restore(const QString& path, const core::Lines& lines,
                            const core::CropRect& cropRect, int rotationQuarters,
                            const QImage& decoded) {
    ++pictureGen;
    if (!path.isEmpty() && !decoded.isNull()) {
      blankPage = false;   // the owner re-marks reopened blanks after restore
      originalImage = decoded;
      imagePath = path;
      // Rotation must be set before defaultCropRect / rebuild read it.
      this->rotationQuarters = ((rotationQuarters % 4) + 4) % 4;
      // Re-apply the stored crop, or default-crop sessions saved before
      // cropping existed (cropRect.width == 0).
      this->cropRect = cropRect.width > 0 ? cropRect : defaultCropRect();
      rebuildCroppedFromOriginal();
    }
    this->lines = lines;
    currentLine = core::Line{};
    applyDefaultsToCurrent();
    filterDirty = true;
    resetSteps();
  }

  void CanvasScene::rotateImage(bool clockwise) {
    if (originalImage.isNull()) return;
    // The lines turn inside the old crop box, the window follows into the turned space.
    const core::EditTurn turned = core::rotateEditQuarter(lines, cropRect, rotationQuarters, originalImage.width(),
                                                          originalImage.height(), clockwise);
    cropRect = turned.crop;
    rotationQuarters = turned.quarters;
    rebuildCroppedFromOriginal();
    currentLine = core::Line{};
    applyDefaultsToCurrent();
    pushStep();   // a turn is one undo step, as the browser's
  }

  void CanvasScene::applyCrop(const core::CropRect& rect, bool recalc) {
    if (originalImage.isNull()) return;
    // Snap to integer pixels within the rotated original (the crop's pixel space).
    const QSize rot = model::turnedSize(originalImage.size(), rotationQuarters);
    const core::CropRect nr = core::snapCropRect(rect, rot.width(), rot.height());

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
    // A user or plan crop is an undo step; the crop a load places (no recalc) starts the stack.
    if (recalc) pushStep();
    else resetSteps();
  }

  void CanvasScene::setLines(const core::Lines& lines) {
    commitLines(lines);
    resetSteps();   // a fresh document: the stack starts here
  }

  void CanvasScene::commitLines(const core::Lines& lines) {
    this->lines = lines;
    currentLine = core::Line{};
    applyDefaultsToCurrent();
    pushStep();
  }

  bool CanvasScene::commitLayout(const core::Lines& lines, const core::CropRect& crop, int quarters) {
    core::EditorMemento step = memento();
    step.lines = lines;
    step.crop = crop;
    step.quarters = ((quarters % 4) + 4) % 4;
    const bool rebuilt = restoreMemento(step);
    pushStep();
    return rebuilt;
  }

  core::EditorMemento CanvasScene::memento() const {
    return {lines, true, cropRect, rotationQuarters, imageFilter.toStdString(),
            filterColor.name(QColor::HexRgb).toStdString()};
  }

  void CanvasScene::pushStep() {
    history.push(memento());
    stepFilter = imageFilter;
    stepTint = filterColor;
  }

  void CanvasScene::resetSteps() {
    history.reset(memento());
    stepFilter = imageFilter;
    stepTint = filterColor;
  }

  bool CanvasScene::undo() {
    const std::optional<core::EditorMemento> step = history.undo();
    if (step) restoreMemento(*step);
    return step.has_value();
  }

  bool CanvasScene::redo() {
    const std::optional<core::EditorMemento> step = history.redo();
    if (step) restoreMemento(*step);
    return step.has_value();
  }

  // The rebuild is the one a crop runs, from the original: no decode. Browser twin: restoreView. A
  // step without a filter leaves the one on screen.
  bool CanvasScene::restoreMemento(const core::EditorMemento& m) {
    lines = m.lines;
    currentLine = core::Line{};
    applyDefaultsToCurrent();
    if (!m.filter.empty()) {
      const QString mode = QString::fromStdString(m.filter);
      const QColor tint = m.filterColor.empty() ? filterColor : QColor(QString::fromStdString(m.filterColor));
      if (mode != imageFilter || tint.name() != filterColor.name()) setImageFilter(mode, tint);
    }
    stepFilter = imageFilter;
    stepTint = filterColor;
    const core::CropRect& c = cropRect;
    const bool sameView = m.quarters == rotationQuarters && m.crop.x == c.x && m.crop.y == c.y &&
                          m.crop.width == c.width && m.crop.height == c.height;
    if (!m.hasView || originalImage.isNull() || sameView) return false;
    rotationQuarters = m.quarters;
    cropRect = m.crop;
    rebuildCroppedFromOriginal();
    return true;
  }

}  // namespace stencil::gui

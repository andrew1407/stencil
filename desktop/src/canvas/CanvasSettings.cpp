#include "CanvasWidget.hpp"
#include "CanvasWidget.hpp"
#include "imageFilter.hpp"
#include "../support/rowWork.hpp"

#include <cstdint>

// The lines the canvas holds, and every appearance setting applied to them.

namespace stencil::gui {

  core::Lines CanvasWidget::allLines() const {
    core::Lines all = lines_;
    if (!currentLine_.points.empty()) all.push_back(currentLine_);
    return all;
  }

  void CanvasWidget::setLines(const core::Lines& lines) {
    commitLines(lines);
    history_.reset(lines_);   // a fresh document: the stack starts here
  }

  // A scripted or planned edit lands here instead: the user's undo stack survives and the
  // change becomes one step on it, where setLines drops the stack on the floor.
  void CanvasWidget::commitLines(const core::Lines& lines) {
    resetStrokeFx();
    lines_ = lines;
    clearHoverCache();   // indices are meaningless against the new set
    currentLine_ = core::Line{};
    applyDefaultsToCurrent();
    selectedPoint_ = -1;
    selectedLineIdx_ = -1;
    continueLineIdx_ = continueInsertIdx_ = -1;
    commitHistory();   // pushes the snapshot and emits changed()
    update();
    emit selectionChanged();
  }

  void CanvasWidget::setScale(double scale) {
    scale_ = scale;
    if (!image_.isNull()) {
      setFixedSize(QSize(qRound(image_.width() * scale_),
                         qRound(image_.height() * scale_)));
    }
    update();
  }

  void CanvasWidget::setDefaults(const QString& color, double thickness,
                                 double pointSize, const QString& style,
                                 const QString& pointColor) {
    defColor_ = color;
    defThickness_ = thickness;
    defPointSize_ = pointSize;
    defStyle_ = style;
    defPointColor_ = pointColor;
    if (currentLine_.points.empty()) applyDefaultsToCurrent();
    update();
  }

  void CanvasWidget::applyDefaultsToCurrent() {
    currentLine_.color = defColor_.toStdString();
    currentLine_.thickness = defThickness_;
    currentLine_.pointSize = defPointSize_;
    currentLine_.style = defStyle_.toStdString();
    // Resolved AT DRAW TIME (empty setting → the current line colour) so a later
    // line-colour change never recolours already-drawn points (browser parity).
    currentLine_.pointColor =
        (defPointColor_.isEmpty() ? defColor_ : defPointColor_).toStdString();
  }

  void CanvasWidget::setShowPoints(bool on) {
    showPoints_ = on;
    update();
  }
  void CanvasWidget::setShowLines(bool on) {
    showLines_ = on;
    update();
  }
  void CanvasWidget::setDark(bool dark) {
    dark_ = dark;
    update();
  }

  void CanvasWidget::setAccent(const QString& accentKey) {
    accentKey_ = accentKey;
    update();
  }

  void CanvasWidget::setHighlightColors(const QColor& selGlow, const QColor& hoverRing,
                                        const QColor& focusRing) {
    if (selGlow.isValid()) selGlow_ = selGlow;
    if (hoverRing.isValid()) hoverRing_ = hoverRing;
    if (focusRing.isValid()) focusRing_ = focusRing;
    update();
  }

  // image filters (port of browser/js/core/renderer.js
  // drawImageWithFilter ~9 + #applyTintFilter ~164)
  void CanvasWidget::setFilter(const QString& mode) {
    imageFilter_ = mode;
    filterDirty_ = true;
    update();
  }

  void CanvasWidget::setFilterColor(const QColor& tint) {
    filterColor_ = tint;
    filterDirty_ = true;
    update();
  }

  void CanvasWidget::setImageFilter(const QString& mode, const QColor& tint) {
    imageFilter_ = mode;
    filterColor_ = tint;
    filterDirty_ = true;
    update();  // single repaint for both
  }

  // compare view (port of browser DrawingApp / renderer.js)
  void CanvasWidget::setCompareMode(const QString& mode) {
    if (mode != "none" && mode != "original" && mode != "vertical" && mode != "horizontal")
      return;
    if (compareMode_ == mode) return;
    compareMode_ = mode;
    update();
  }

  void CanvasWidget::setCompareSplit(double fraction) {
    compareSplit_ = std::clamp(fraction, 0.02, 0.98);
    if (compareMode_ == "vertical" || compareMode_ == "horizontal") update();
  }

  void CanvasWidget::setCompareHoldOriginal(bool on) {
    if (compareHoldOriginal_ == on) return;
    compareHoldOriginal_ = on;
    update();
  }

  // Loads reset this to false; the owner re-marks blanks right after creating,
  // recolouring, or reopening one.
  void CanvasWidget::setBlankPage(bool on) {
    if (blankPage_ == on) return;
    blankPage_ = on;
    update();
  }

  // Rebuild filteredImage_ from image_ per the active filter, spread over the thread pool by row.
  // The pixel math lives once in core (shared with the wasm build); the row split is the adapter's.
  // Format_RGBA8888 is core's byte order and is tightly packed, so a row starts at y * width * 4.
  void CanvasWidget::rebuildFilteredImage() {
    filterDirty_ = false;
    if (image_.isNull() || imageFilter_ == "none") {
      filteredImage_ = QImage();
      return;
    }
    const core::FilterMode mode =
        core::filterModeFromString(imageFilter_.toStdString());
    QImage img = image_.convertToFormat(QImage::Format_RGBA8888);
    const int w = img.width();
    const int h = img.height();
    std::uint8_t* bits = img.bits();
    // Enough rows that a slice outweighs handing it to another thread.
    constexpr int MIN_ROWS_PER_SLICE = 64;
    if (mode == core::FilterMode::CONTOUR) {
      // Sobel reads one row OUTSIDE its range on each side, so every luma row must
      // exist before any sobel slice runs: two separate phases, never interleaved
      // per tile (core/raster/imageFilter.hpp).
      std::vector<std::uint8_t> luma(static_cast<std::size_t>(w) * h);
      support::forEachSlice(h, MIN_ROWS_PER_SLICE, [&](int y0, int y1) {
        core::buildLumaRows(bits, w, h, y0, y1, luma.data());
      });
      support::forEachSlice(h, MIN_ROWS_PER_SLICE, [&](int y0, int y1) {
        core::sobelRows(luma.data(), bits, w, h, y0, y1);
      });
      filteredImage_ = img;
      return;
    }
    // The tint channels are read only for the custom mode.
    const int tr = filterColor_.red();
    const int tg = filterColor_.green();
    const int tb = filterColor_.blue();
    support::forEachSlice(h, MIN_ROWS_PER_SLICE, [&](int y0, int y1) {
      core::applyFilterRows(mode, bits, w, y0, y1, tr, tg, tb);
    });
    filteredImage_ = img;
  }

}  // namespace stencil::gui

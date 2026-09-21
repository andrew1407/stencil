#include "CanvasWidget.hpp"
#include "CanvasWidget.hpp"
#include "imageFilter.hpp"
#include "../support/rowWork.hpp"

#include <cstdint>

// The lines the canvas holds, and every appearance setting applied to them.

namespace stencil::gui {

  core::Lines CanvasWidget::allLines() const {
    core::Lines all = lines;
    if (!currentLine.points.empty()) all.push_back(currentLine);
    return all;
  }

  void CanvasWidget::setLines(const core::Lines& lines) {
    commitLines(lines);
    history.reset(this->lines);   // a fresh document: the stack starts here
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

  void CanvasWidget::setScale(double scale) {
    this->scale = scale;
    if (!image.isNull()) {
      setFixedSize(QSize(qRound(image.width() * this->scale),
                         qRound(image.height() * this->scale)));
    }
    update();
  }

  void CanvasWidget::setDefaults(const QString& color, double thickness,
                                 double pointSize, const QString& style,
                                 const QString& pointColor) {
    defColor = color;
    defThickness = thickness;
    defPointSize = pointSize;
    defStyle = style;
    defPointColor = pointColor;
    if (currentLine.points.empty()) applyDefaultsToCurrent();
    update();
  }

  void CanvasWidget::applyDefaultsToCurrent() {
    currentLine.color = defColor.toStdString();
    currentLine.thickness = defThickness;
    currentLine.pointSize = defPointSize;
    currentLine.style = defStyle.toStdString();
    // Resolved AT DRAW TIME (empty setting → the current line colour) so a later
    // line-colour change never recolours already-drawn points (browser parity).
    currentLine.pointColor =
        (defPointColor.isEmpty() ? defColor : defPointColor).toStdString();
  }

  void CanvasWidget::setShowPoints(bool on) {
    showPoints = on;
    update();
  }
  void CanvasWidget::setShowLines(bool on) {
    showLines = on;
    update();
  }
  void CanvasWidget::setDark(bool dark) {
    this->dark = dark;
    update();
  }

  void CanvasWidget::setAccent(const QString& accentKey) {
    this->accentKey = accentKey;
    update();
  }

  void CanvasWidget::setHighlightColors(const QColor& selGlow, const QColor& hoverRing,
                                        const QColor& focusRing) {
    if (selGlow.isValid()) this->selGlow = selGlow;
    if (hoverRing.isValid()) this->hoverRing = hoverRing;
    if (focusRing.isValid()) this->focusRing = focusRing;
    update();
  }

  // image filters (port of browser/js/core/draw/renderer.js
  // drawImageWithFilter ~9 + #applyTintFilter ~164)
  void CanvasWidget::setFilter(const QString& mode) {
    imageFilter = mode;
    filterDirty = true;
    update();
  }

  void CanvasWidget::setFilterColor(const QColor& tint) {
    filterColor = tint;
    filterDirty = true;
    update();
  }

  void CanvasWidget::setImageFilter(const QString& mode, const QColor& tint) {
    imageFilter = mode;
    filterColor = tint;
    filterDirty = true;
    update();  // single repaint for both
  }

  // compare view (port of browser DrawingApp / renderer.js)
  void CanvasWidget::setCompareMode(const QString& mode) {
    if (mode != "none" && mode != "original" && mode != "vertical" && mode != "horizontal")
      return;
    if (compareMode == mode) return;
    compareMode = mode;
    update();
  }

  void CanvasWidget::setCompareSplit(double fraction) {
    compareSplit = std::clamp(fraction, 0.02, 0.98);
    if (compareMode == "vertical" || compareMode == "horizontal") update();
  }

  void CanvasWidget::setCompareHoldOriginal(bool on) {
    if (compareHoldOriginal == on) return;
    compareHoldOriginal = on;
    update();
  }

  // Loads reset this to false; the owner re-marks blanks right after creating,
  // recolouring, or reopening one.
  void CanvasWidget::setBlankPage(bool on) {
    if (blankPage == on) return;
    blankPage = on;
    update();
  }

  // The pixel math lives once in core (shared with the wasm build); the row split is the adapter's.
  // Format_RGBA8888 is core's byte order and tightly packed, so a row starts at y * width * 4.
  void CanvasWidget::rebuildFilteredImage() {
    filterDirty = false;
    if (image.isNull() || imageFilter == "none") {
      filteredImage = QImage();
      return;
    }
    const core::FilterMode mode =
        core::filterModeFromString(imageFilter.toStdString());
    QImage img = image.convertToFormat(QImage::Format_RGBA8888);
    const int w = img.width();
    const int h = img.height();
    std::uint8_t* bits = img.bits();
    // Enough rows that a slice outweighs handing it to another thread.
    constexpr int MIN_ROWS_PER_SLICE = 64;
    if (mode == core::FilterMode::CONTOUR) {
      // Sobel reads one row OUTSIDE its range each side, so every luma row must exist before any
      // sobel slice runs: two phases, never interleaved per tile (core/raster/imageFilter.hpp).
      std::vector<std::uint8_t> luma(static_cast<std::size_t>(w) * h);
      support::forEachSlice(h, MIN_ROWS_PER_SLICE, [&](int y0, int y1) {
        core::buildLumaRows(bits, w, h, y0, y1, luma.data());
      });
      support::forEachSlice(h, MIN_ROWS_PER_SLICE, [&](int y0, int y1) {
        core::sobelRows(luma.data(), bits, w, h, y0, y1);
      });
      filteredImage = img;
      return;
    }
    // The tint channels are read only for the custom mode.
    const int tr = filterColor.red();
    const int tg = filterColor.green();
    const int tb = filterColor.blue();
    support::forEachSlice(h, MIN_ROWS_PER_SLICE, [&](int y0, int y1) {
      core::applyFilterRows(mode, bits, w, y0, y1, tr, tg, tb);
    });
    filteredImage = img;
  }

}  // namespace stencil::gui

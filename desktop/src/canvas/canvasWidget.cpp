#include "canvasWidget.hpp"
#include "geometry.hpp"
#include "imageFilter.hpp"
#include "theme.hpp"
#include <algorithm>
#include <cmath>
#include <QColor>
#include <QCursor>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QVariantAnimation>
#include <QTransform>
#include <QNativeGestureEvent>
#include <QWheelEvent>

namespace stencil::gui {

  CanvasWidget::CanvasWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumSize(320, 240);
    setFocusPolicy(Qt::StrongFocus);
    applyDefaultsToCurrent();
    // Wheel edits (thickness/rotation) mutate live and commit one undo step once
    // the wheel goes quiet, mirroring the browser's debounced saveHistory.
    editCommitTimer_.setSingleShot(true);
    editCommitTimer_.setInterval(280);
    connect(&editCommitTimer_, &QTimer::timeout, this,
            [this] { commitHistory(); });
    // Hold-to-draw: while a hold is engaged, tick the controller (~40 ms) so the
    // hold/dwell thresholds fire even when the cursor is held perfectly still.
    holdClock_.start();
    holdTimer_.setInterval(40);
    connect(&holdTimer_, &QTimer::timeout, this, [this] { handleHoldTick(); });
    // Watch modifier key changes app-wide so the tooltip/cursor refresh on
    // Shift/Ctrl/Alt without needing a mouse move (see eventFilter).
    qApp->installEventFilter(this);
  }

  bool CanvasWidget::loadImage(const QString& path) {
    QImage img;
    if (!img.load(path)) return false;
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

  // ── crop (shared cropGeometry; mirrors browser DrawingApp) ──
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
                             int rotationQuarters) {
    scale_ = scale > 0 ? scale : 1.0;
    if (!path.isEmpty()) {
      QImage img;
      if (img.load(path)) {
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

  core::Lines CanvasWidget::allLines() const {
    core::Lines all = lines_;
    if (!currentLine_.points.empty()) all.push_back(currentLine_);
    return all;
  }

  void CanvasWidget::setLines(const core::Lines& lines) {
    lines_ = lines;
    clearHoverCache();   // indices are meaningless against the new set
    currentLine_ = core::Line{};
    applyDefaultsToCurrent();
    selectedPoint_ = -1;
    selectedLineIdx_ = -1;
    continueLineIdx_ = continueInsertIdx_ = -1;
    history_.reset(lines_);
    update();
    emit changed();  // setLines must signal a content change
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

  // ── image filters (port of browser/js/core/renderer.js
  // drawImageWithFilter ~9 + #applyTintFilter ~164) ──
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

  // ── compare view (port of browser DrawingApp / renderer.js) ──
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

  // Rebuild filteredImage_ from image_ per the active filter. bw/sepia/invert/
  // custom mirror the browser's canvas pixel filters; contour routes through
  // core::applyContourRGBA (a convolution, not a per-pixel map).
  void CanvasWidget::rebuildFilteredImage() {
    filterDirty_ = false;
    if (image_.isNull() || imageFilter_ == "none") {
      filteredImage_ = QImage();
      return;
    }
    const core::FilterMode mode =
        core::filterModeFromString(imageFilter_.toStdString());
    if (mode == core::FilterMode::Contour) {
      // Contour is a whole-grid Sobel convolution, so it can't use the per-pixel
      // loop below. Hand core::applyContourRGBA a Format_RGBA8888 copy — the
      // byte order its luma math expects (ARGB32 would feed swapped channels).
      QImage rgba = image_.convertToFormat(QImage::Format_RGBA8888);
      core::applyContourRGBA(rgba.bits(), rgba.width(), rgba.height());
      filteredImage_ = rgba;
      return;
    }
    QImage img = image_.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width();
    const int h = img.height();

    // Per-pixel color math lives once in core::filterPixel (shared with the
    // WebAssembly browser build); here we only walk the QImage scanlines and
    // unpack/repack QRgb. The tint channels are read only for the custom mode.
    const int tr = filterColor_.red();
    const int tg = filterColor_.green();
    const int tb = filterColor_.blue();
    for (int y = 0; y < h; ++y) {
      QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
      for (int x = 0; x < w; ++x) {
        const QRgb px = row[x];
        const core::Rgb8 o = core::filterPixel(mode, qRed(px), qGreen(px),
                                               qBlue(px), tr, tg, tb);
        row[x] = qRgba(o.r, o.g, o.b, qAlpha(px));
      }
    }
    filteredImage_ = img;
  }

  // Port of drawingApp.js startDrawingMode (~1070): begin a fresh in-progress
  // line and arm the click gate. Requires a loaded image.
  void CanvasWidget::startDrawingMode() {
    if (compareReadOnly()) return;   // read-only compare view
    if (image_.isNull() || isDrawing_) return;
    isDrawing_ = true;

    // Continuation: a committed line is selected -> extend it. New points connect
    // to its tail, or to the focused point if one is selected. Port of
    // drawingApp.js startDrawingMode continuation branch (~1079).
    if (selectedLineIdx_ >= 0 &&
        selectedLineIdx_ < static_cast<int>(lines_.size())) {
      continueLineIdx_ = selectedLineIdx_;
      const core::Line& line = lines_[continueLineIdx_];
      continueInsertIdx_ = (selectedPoint_ >= 0)
                               ? selectedPoint_ + 1
                               : static_cast<int>(line.points.size());
      currentLine_ = core::Line{};  // unused while continuing
      update();
      emit drawingModeChanged(true);
      emit selectionChanged();
      return;
    }

    continueLineIdx_ = -1;
    continueInsertIdx_ = -1;
    currentLine_ = core::Line{};
    applyDefaultsToCurrent();
    selectedPoint_ = -1;
    update();
    emit drawingModeChanged(true);
    emit selectionChanged();
  }

  // Port of drawingApp.js stopDrawingMode (~1138): commit the in-progress line
  // (when it has >= 2 points) and disarm the gate.
  void CanvasWidget::stopDrawingMode() {
    if (!isDrawing_) return;

    // Continuation: the extended line is already in lines_ — just commit & reset,
    // keeping it selected (drawingApp.js stopDrawingMode continuation ~1140).
    if (continueLineIdx_ >= 0) {
      const int li = continueLineIdx_;
      continueLineIdx_ = -1;
      continueInsertIdx_ = -1;
      currentLine_ = core::Line{};
      applyDefaultsToCurrent();
      isDrawing_ = false;
      selectedLineIdx_ = (li < static_cast<int>(lines_.size())) ? li : -1;
      commitHistory();
      update();
      emit drawingModeChanged(false);
      emit selectionChanged();
      return;
    }

    if (currentLine_.points.size() >= 2) {
      lines_.push_back(currentLine_);
      commitHistory();
    }
    currentLine_ = core::Line{};
    applyDefaultsToCurrent();
    isDrawing_ = false;
    selectedPoint_ = -1;
    selectedLineIdx_ = -1;
    continueLineIdx_ = continueInsertIdx_ = -1;
    update();
    emit drawingModeChanged(false);
    emit selectionChanged();
  }

  void CanvasWidget::startNewLine() {
    if (compareReadOnly()) return;   // read-only compare view
    if (currentLine_.points.size() >= 2) {
      lines_.push_back(currentLine_);
      commitHistory();
    }
    currentLine_ = core::Line{};
    applyDefaultsToCurrent();
    selectedPoint_ = -1;
    update();
    emit selectionChanged();
  }

  void CanvasWidget::deleteLastPoint() {
    if (compareReadOnly()) return;   // read-only compare view
    if (currentLine_.points.empty()) return;
    currentLine_.points.pop_back();
    clearHoverCache();   // the hovered in-progress point may be the one removed
    selectedPoint_ = -1;
    update();
    emit selectionChanged();
  }

  void CanvasWidget::clearAll() {
    if (compareReadOnly()) return;   // read-only compare view
    if (lines_.empty() && currentLine_.points.empty()) return;
    lines_.clear();
    clearHoverCache();
    currentLine_ = core::Line{};
    applyDefaultsToCurrent();
    selectedPoint_ = -1;
    selectedLineIdx_ = -1;
    continueLineIdx_ = continueInsertIdx_ = -1;
    commitHistory();
    update();
    emit selectionChanged();
  }

  void CanvasWidget::commitHistory() {
    history_.push(lines_);
    emit changed();
  }

  void CanvasWidget::undo() {
    if (compareReadOnly()) return;   // read-only compare view
    if (auto snap = history_.undo()) {
      lines_ = *snap;
      clearHoverCache();   // the snapshot may not contain the hovered indices
      currentLine_ = core::Line{};
      applyDefaultsToCurrent();
      selectedPoint_ = -1;
      selectedLineIdx_ = -1;
      update();
      emit changed();
      emit selectionChanged();
    }
  }

  void CanvasWidget::redo() {
    if (compareReadOnly()) return;   // read-only compare view
    if (auto snap = history_.redo()) {
      lines_ = *snap;
      clearHoverCache();   // see undo()
      currentLine_ = core::Line{};
      applyDefaultsToCurrent();
      selectedPoint_ = -1;
      selectedLineIdx_ = -1;
      update();
      emit changed();
      emit selectionChanged();
    }
  }

  // The line the selection panel shows: an explicitly selected committed line
  // wins, else the in-progress line while drawing, else the last committed one.
  // Mutable forwarder: const_cast the const overload's result; never recurse.
  core::Line* CanvasWidget::mutablePanelLine() {
    return const_cast<core::Line*>(
        static_cast<const CanvasWidget*>(this)->panelLine());
  }

  const core::Line* CanvasWidget::panelLine() const {
    if (selectedLineIdx_ >= 0 &&
        selectedLineIdx_ < static_cast<int>(lines_.size())) {
      return &lines_[selectedLineIdx_];
    }
    if (!currentLine_.points.empty()) return &currentLine_;
    if (!lines_.empty()) return &lines_.back();
    return nullptr;
  }

  void CanvasWidget::selectPoint(int index) {
    selectedPoint_ = index;
    update();
  }

  void CanvasWidget::deletePoint(int index) {
    if (compareReadOnly()) return;   // read-only compare view
    core::Line* line = mutablePanelLine();
    if (!line || index < 0 ||
        index >= static_cast<int>(line->points.size())) {
      return;
    }
    const bool committed = (line != &currentLine_);
    line->points.erase(line->points.begin() + index);
    if (committed) {
      // Erase the line the panel actually shows (it need not be lines_.back()).
      if (line->points.empty())
        lines_.erase(lines_.begin() + (line - lines_.data()));
      selectedLineIdx_ = -1;  // index may now be stale/invalid
      commitHistory();
    }
    clearHoverCache();   // indices shifted
    selectedPoint_ = -1;
    update();
    emit changed();
    emit selectionChanged();
  }

  void CanvasWidget::setPointCoord(int index, int axis, double value) {
    if (compareReadOnly()) return;   // read-only compare view
    core::Line* line = mutablePanelLine();
    if (!line || index < 0 || index >= static_cast<int>(line->points.size())) return;
    if (!std::isfinite(value)) return;
    if (axis == 0) line->points[index].x = value;
    else line->points[index].y = value;
    if (line != &currentLine_) commitHistory();  // committed line → undoable edit
    update();
    emit changed();
    emit selectionChanged();  // refresh the panel's coord display (cm column, length)
  }

  void CanvasWidget::deselect() {
    selectedPoint_ = -1;
    selectedLineIdx_ = -1;
    continueLineIdx_ = continueInsertIdx_ = -1;
    update();
    emit selectionChanged();
  }

  // port of browser drawingApp.js setDrawMode ~1120. Guard equality so we
  // don't churn repaints / re-emit when nothing actually changed.
  void CanvasWidget::setDrawMode(DrawMode mode) {
    if (drawMode_ == mode) return;
    drawMode_ = mode;
    emit drawModeChanged(drawMode_);
  }

  // Hit-test committed lines, topmost match wins (drawingApp.js ~1251): a point
  // hit selects its line AND focuses that point (the rotation pivot); a segment
  // hit selects with no focused point; empty space clears the selection.
  int CanvasWidget::selectLineAt(double x, double y) {
    selectedLines_.clear();   // a plain click leaves multi-select mode
    if (auto pt = core::findNearestPoint(lines_, x, y, hitRadius(12.0))) {
      selectedLineIdx_ = pt->lineIdx;
      selectedPoint_ = pt->ptIdx;
    } else {
      selectedLineIdx_ = core::findLineAt(lines_, x, y, hitRadius(8.0));
      selectedPoint_ = -1;
    }
    update();
    emit selectionChanged();
    return selectedLineIdx_;
  }

  std::vector<int> CanvasWidget::selectedIndices() const {
    std::vector<int> out;
    const int n = static_cast<int>(lines_.size());
    if (!selectedLines_.empty()) {
      for (int i : selectedLines_)
        if (i >= 0 && i < n) out.push_back(i);
    } else if (selectedLineIdx_ >= 0 && selectedLineIdx_ < n) {
      out.push_back(selectedLineIdx_);
    }
    return out;
  }

  bool CanvasWidget::isLineSelected(int i) const {
    if (!selectedLines_.empty())
      return std::find(selectedLines_.begin(), selectedLines_.end(), i) != selectedLines_.end();
    return i == selectedLineIdx_;
  }

  // Toggle line `idx` in/out of the multi-select set (Ctrl+Shift+click). Shared by the
  // point-hit and Lines-tab-row entry points.
  void CanvasWidget::toggleLineIndex(int idx) {
    if (idx < 0 || idx >= static_cast<int>(lines_.size())) return;
    // Seed the set from the current single selection on the first Ctrl+Shift+click.
    if (selectedLines_.empty() && selectedLineIdx_ >= 0 && selectedLineIdx_ != idx)
      selectedLines_.push_back(selectedLineIdx_);
    auto it = std::find(selectedLines_.begin(), selectedLines_.end(), idx);
    if (it != selectedLines_.end()) selectedLines_.erase(it);
    else selectedLines_.push_back(idx);
    if (selectedLines_.size() == 1) {
      selectedLineIdx_ = selectedLines_.front();  // back to single-select (its editor returns)
      selectedLines_.clear();
    } else {
      selectedLineIdx_ = -1;  // 0 or 2+ selected → no single-line editor
    }
    selectedPoint_ = -1;
    update();
    emit selectionChanged();
  }

  void CanvasWidget::toggleLineSelection(const core::Point& ip) {
    int idx = -1;
    if (auto pt = core::findNearestPoint(lines_, ip.x, ip.y, hitRadius(12.0))) idx = pt->lineIdx;
    else idx = core::findLineAt(lines_, ip.x, ip.y, hitRadius(8.0));
    toggleLineIndex(idx);  // idx == -1 (empty space) is a no-op inside
  }

  // Single-select line `idx` from the Lines-tab list. Mirrors selectLineAt's post-hit
  // state (clears multi-select, no focused point). Port of drawingApp.js selectLineFromList.
  void CanvasWidget::selectLineByIndex(int idx) {
    if (idx < 0 || idx >= static_cast<int>(lines_.size())) return;
    selectedLines_.clear();
    selectedLineIdx_ = idx;
    selectedPoint_ = -1;
    update();
    emit selectionChanged();
  }

  // Index-keyed twin of toggleLineSelection(point): Ctrl+Shift+click a Lines-tab row.
  void CanvasWidget::toggleLineSelectionByIndex(int idx) { toggleLineIndex(idx); }

  // Remove committed line `idx` (Lines-tab 🗑). Keeps the single + multi selection and the
  // focused point consistent with the now-shifted indices. Port of drawingApp.js removeLine.
  void CanvasWidget::removeLineByIndex(int idx) {
    if (compareReadOnly()) return;   // read-only compare view
    if (idx < 0 || idx >= static_cast<int>(lines_.size())) return;
    lines_.erase(lines_.begin() + idx);
    clearHoverCache();   // indices shifted
    for (auto it = selectedLines_.begin(); it != selectedLines_.end();) {
      if (*it == idx) { it = selectedLines_.erase(it); continue; }
      if (*it > idx) --*it;
      ++it;
    }
    if (selectedLineIdx_ == idx) { selectedLineIdx_ = -1; selectedPoint_ = -1; }
    else if (selectedLineIdx_ > idx) --selectedLineIdx_;
    // A removal that leaves exactly one multi-selected line drops back to single-select.
    if (selectedLines_.size() == 1) { selectedLineIdx_ = selectedLines_.front(); selectedLines_.clear(); }
    commitHistory();
    update();
    emit changed();
    emit selectionChanged();
  }

  // Mutable forwarder: const_cast the const overload's result (renderToImage
  // pattern); never recurse through the mutable version.
  core::Line* CanvasWidget::selectedLine() {
    return const_cast<core::Line*>(
        static_cast<const CanvasWidget*>(this)->selectedLine());
  }

  const core::Line* CanvasWidget::selectedLine() const {
    if (selectedLineIdx_ < 0 ||
        selectedLineIdx_ >= static_cast<int>(lines_.size())) {
      return nullptr;
    }
    return &lines_[selectedLineIdx_];
  }

  // port of browser drawingApp.js createRect ~1393 (standalone branch). Build
  // a locked 4-corner rectangle (drawPolygon closes the loop, so no 5th point),
  // push it, select it, and commit history.
  void CanvasWidget::createRect(double x1, double y1, double x2, double y2) {
    const double xa = std::min(x1, x2);
    const double xb = std::max(x1, x2);
    const double ya = std::min(y1, y2);
    const double yb = std::max(y1, y2);

    // Continuation drawing: append the 4 corners to the line being extended
    // instead of making a standalone area (drawingApp.js createRect ~1402).
    if (continueLineIdx_ >= 0 &&
        continueLineIdx_ < static_cast<int>(lines_.size())) {
      core::Line& line = lines_[continueLineIdx_];
      const int at = std::max(
          0, std::min(continueInsertIdx_, static_cast<int>(line.points.size())));
      const core::Point corners[4] = {
          {xa, ya}, {xb, ya}, {xb, yb}, {xa, yb}};
      line.points.insert(line.points.begin() + at, corners, corners + 4);
      continueInsertIdx_ = at + 4;
      selectedPoint_ = continueInsertIdx_ - 1;
      commitHistory();
      update();
      emit selectionChanged();
      return;
    }

    core::Line rect;
    rect.points = {{xa, ya}, {xb, ya}, {xb, yb}, {xa, yb}};  // exactly 4 corners
    rect.color = defColor_.toStdString();
    rect.pointColor =
        (defPointColor_.isEmpty() ? defColor_ : defPointColor_).toStdString();
    rect.thickness = defThickness_;
    rect.pointSize = defPointSize_;
    rect.style = defStyle_.toStdString();
    rect.locked = true;
    rect.fillColor = "transparent";

    lines_.push_back(rect);
    selectedLineIdx_ = static_cast<int>(lines_.size()) - 1;
    selectedPoint_ = -1;
    commitHistory();
    update();
    emit selectionChanged();
  }

  core::Point CanvasWidget::toImageSpace(int widgetX, int widgetY) const {
    return {widgetX / scale_, widgetY / scale_};
  }

  // Port of the line drawing in browser/js/core/renderer.js: locked-area fill
  // beneath the stroke, dash patterns per style, then points. Scale-parameterized
  // so renderToImage draws at native resolution while the live view passes scale_.
  void CanvasWidget::drawLineScaled(QPainter& p, const core::Line& line,
                                    int lineIdx, double scale,
                                    bool highlight) const {
    if (line.points.empty()) return;

    QPolygonF poly;
    for (const auto& pt : line.points) {
      poly << QPointF(pt.x * scale, pt.y * scale);
    }

    const QColor stroke(QString::fromStdString(line.color));
    Palette pal = themePalette(dark_, accentKey_);
    // The user's own highlight-style choices (Settings), not the theme's fixed
    // defaults — setHighlightColors() is what actually moves these off DEFAULT_VISUALS.
    pal.selGlow = selGlow_;
    pal.hoverRing = hoverRing_;

    // Points take the line's own point colour, which core::pointColorOr resolves to the
    // stroke colour when unset — so a line without one paints exactly as it always did.
    // An unparseable colour falls back to the stroke rather than painting points black.
    const QColor pointParsed(QString::fromStdString(core::pointColorOr(line)));
    const QColor pointFill = pointParsed.isValid() ? pointParsed : stroke;

    drawFill(p, line, poly);
    drawGlow(p, line, poly, lineIdx, highlight, pal);
    drawStroke(p, line, poly, stroke);
    drawPoints(p, line, poly, lineIdx, highlight, pointFill, pal);
  }

  // Locked-area fill beneath the stroke (renderer.js): only for closed shapes
  // with a non-transparent fill while lines are shown.
  void CanvasWidget::drawFill(QPainter& p, const core::Line& line,
                              const QPolygonF& poly) const {
    if (showLines_ && line.locked && line.points.size() >= 3 &&
        line.fillColor != "transparent" && !line.fillColor.empty()) {
      p.setBrush(QColor(QString::fromStdString(line.fillColor)));
      p.setPen(Qt::NoPen);
      p.drawPolygon(poly);
    }
  }

  // Selection glow beneath the stroke (renderer.js drawLine ~77): a fat, semi-
  // transparent halo around the selected committed line. Live view only.
  void CanvasWidget::drawGlow(QPainter& p, const core::Line& line,
                              const QPolygonF& poly, int lineIdx, bool highlight,
                              const Palette& pal) const {
    // Lines-list row hover: a thinner, fainter glow than the selection's, so the
    // two states stay distinguishable (browser renderer.js listHoverLineIdx).
    if (highlight && showLines_ && lineIdx >= 0 && lineIdx == listHoverLineIdx_ &&
        !isLineSelected(lineIdx) && line.points.size() >= 2) {
      QColor glow = pal.hoverRing;
      glow.setAlphaF(0.35);
      QPen gpen(glow);
      gpen.setWidthF(line.thickness + 6.0);
      gpen.setCapStyle(Qt::RoundCap);
      gpen.setJoinStyle(Qt::RoundJoin);
      p.setPen(gpen);
      p.setBrush(Qt::NoBrush);
      if (line.locked) p.drawPolygon(poly);
      else p.drawPolyline(poly);
    }
    if (highlight && showLines_ && lineIdx >= 0 && isLineSelected(lineIdx) &&
        line.points.size() >= 2) {
      QColor glow = pal.selGlow;
      glow.setAlphaF(0.6);
      QPen gpen(glow);
      gpen.setWidthF(line.thickness + 8.0);
      gpen.setCapStyle(Qt::RoundCap);
      gpen.setJoinStyle(Qt::RoundJoin);
      p.setPen(gpen);
      p.setBrush(Qt::NoBrush);
      if (line.locked) p.drawPolygon(poly);
      else p.drawPolyline(poly);
    }
  }

  // The stroke itself: width/cap/join + dash pattern per style.
  void CanvasWidget::drawStroke(QPainter& p, const core::Line& line,
                                const QPolygonF& poly,
                                const QColor& stroke) const {
    if (showLines_) {
      QPen pen(stroke);
      pen.setWidthF(line.thickness);
      pen.setCapStyle(Qt::RoundCap);
      pen.setJoinStyle(Qt::RoundJoin);
      if (line.style == "dashed") pen.setDashPattern({10.0, 5.0});
      else if (line.style == "dotted") pen.setDashPattern({2.0, 5.0});
      p.setPen(pen);
      p.setBrush(Qt::NoBrush);
      if (line.points.size() >= 2) {
        if (line.locked) p.drawPolygon(poly);
        else p.drawPolyline(poly);
      }
    }
  }

  // Points + hover/focus rings. The browser draws no point labels on the canvas
  // (renderer.js), so neither do we. Rings are live-only (never baked into exports).
  void CanvasWidget::drawPoints(QPainter& p, const core::Line& line,
                                 const QPolygonF& poly, int lineIdx,
                                 bool highlight, const QColor& stroke,
                                 const Palette& pal) const {
    if (!showPoints_) return;

    const bool isActive = highlight && (&line == panelLine());
    const double r = line.pointSize;
    p.setPen(QPen(pal.textMain, 1));
    for (int i = 0; i < poly.size(); ++i) {
      const QPointF v = poly[i];
      // Focused point: filled selection glow + bold ring (renderer.js state 2,
      // focusRingColor — Settings-backed, browser DEFAULT_VISUALS.focusRingColor).
      if (isActive && i == selectedPoint_) {
        p.setBrush(pal.selGlow);
        p.setPen(QPen(focusRing_, 2));
        p.drawEllipse(v, r + 3, r + 3);
        p.setPen(QPen(pal.textMain, 1));
      }
      // Hovered point: thin translucent ring (renderer.js state 1) — from the
      // canvas cursor, a hovered points-table row, or a hovered Lines-list row.
      // Skipped on the focused point, which has the bolder ring above.
      else if (highlight &&
               ((lineIdx == hoverLineIdx_ && i == hoverPointIdx_) ||
                (isActive && i == listHoverPointIdx_) ||
                (lineIdx >= 0 && lineIdx == listHoverLineIdx_))) {
        QColor ring = pal.hoverRing;
        ring.setAlphaF(0.55);
        QPen rpen(ring);
        rpen.setWidthF(1.8);
        p.setBrush(Qt::NoBrush);
        p.setPen(rpen);
        p.drawEllipse(v, r + 4, r + 4);
        p.setPen(QPen(pal.textMain, 1));
      }
      p.setBrush(stroke);
      p.drawEllipse(v, r, r);
    }
  }

  void CanvasWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    Palette pal = themePalette(dark_, accentKey_);
    // The user's own highlight-style choices (Settings), not the theme's fixed
    // defaults — setHighlightColors() is what actually moves these off DEFAULT_VISUALS.
    pal.selGlow = selGlow_;
    pal.hoverRing = hoverRing_;
    if (image_.isNull()) {
      p.fillRect(rect(), pal.bgPage);
      // The clear's dust is still falling: paint the bare page and nothing else, so the
      // invitation does not appear underneath the particles (setIdleHintHidden).
      if (idleHintHidden_) { unsetCursor(); return; }
      // The idle card (port of the browser's .idle-create-btn): a dashed box with
      // the 32 px `image` glyph over a "＋ Blank image" label, accent fill on hover.
      // The card alone is the click/cursor target, never the whole page.
      const QString label = QStringLiteral("＋ Blank image");
      constexpr int kIconPx = 32;
      constexpr qreal kGap = 6, kPadX = 26, kPadY = 16;

      QFont cardFont = p.font();
      cardFont.setPixelSize(13);
      const QFontMetricsF fm(cardFont);
      const qreal textW = fm.horizontalAdvance(label);
      const qreal textH = fm.height();
      const QRectF wr(rect());
      QRectF box(0, 0, std::max<qreal>(kIconPx, textW) + kPadX * 2,
                 kIconPx + kGap + textH + kPadY * 2);
      // Never wider/taller than the canvas it sits in, so the dashed border stays whole.
      box.setWidth(std::min(box.width(), wr.width() - 32.0));
      box.setHeight(std::min(box.height(), wr.height() - 32.0));
      box.moveCenter(wr.center());

      QColor accent = accentPrimary(accentKey_);
      if (!accent.isValid()) accent = pal.textMuted;
      // The whole hover is a blend on `t`, mirroring the browser transition: panel fill →
      // solid accent, muted dashed edge → bright, text → white, plus a 3 px lift, a drop
      // shadow and a 1.12× icon (.idle-create-btn / .idle-create-icon in animations.css).
      const double t = idleCardHoverT_;
      const auto mix = [t](const QColor& a, const QColor& b) {
        return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                                a.greenF() + (b.greenF() - a.greenF()) * t,
                                a.blueF() + (b.blueF() - a.blueF()) * t,
                                a.alphaF() + (b.alphaF() - a.alphaF()) * t);
      };
      QColor edgeIdle = accent;
      edgeIdle.setAlpha(dark_ ? 150 : 120);
      const QColor fill = mix(pal.bgControls, accent);
      const QColor edge = mix(edgeIdle, accent.lighter(115));
      const QColor ink = mix(pal.textMain, QColor(Qt::white));
      // Hit-test against the RESTING rect, never the lifted one: if the hover target rose
      // with the card, a cursor on its bottom edge would fall out of it, drop the hover,
      // fall back in, and oscillate. Only the painted box moves.
      idleCardRect_ = box;
      box.translate(0, -3.0 * t);

      // Drop shadow (0 8px 24px): a few expanding rounded rects, since QPainter has no
      // blur. Only while lifted, and skipped entirely at rest so the idle card is flat.
      if (t > 0.01) {
        constexpr int kLayers = 6;
        for (int i = kLayers; i >= 1; --i) {
          const double spread = i * 2.4;
          QColor sh(0, 0, 0);
          sh.setAlphaF(0.05 * t * (1.0 - double(i) / (kLayers + 1)));
          p.setPen(Qt::NoPen);
          p.setBrush(sh);
          p.drawRoundedRect(box.adjusted(-spread, -spread + 8, spread, spread + 8),
                            10 + spread, 10 + spread);
        }
      }
      // Fill first with NO pen, then stroke the dashed border as a separate rounded path.
      // Filling + dash-stroking in one drawRoundedRect call drops the vertical dashed edges
      // on some Qt/macOS builds (leaving only the top/bottom rules), so keep the two apart.
      p.setPen(Qt::NoPen);
      p.setBrush(fill);
      p.drawRoundedRect(box, 10, 10);
      QPen dash(edge, 2, Qt::DashLine);
      dash.setDashPattern({3.0, 2.0});
      p.setPen(dash);
      p.setBrush(Qt::NoBrush);
      QPainterPath border;
      border.addRoundedRect(box, 10, 10);
      p.drawPath(border);

      // Glass sweep: a soft diagonal light band crossing the card, clipped to its
      // rounded box (browser layout.css ::after + @keyframes ui-shimmer).
      if (idleShimmerT_ >= 0.0) {
        const double w = box.width();
        const double x = box.left() - 1.35 * w + idleShimmerT_ * 2.7 * w;
        QLinearGradient band(x, box.top(), x + w * 0.9, box.bottom());
        QColor glass(Qt::white);
        glass.setAlphaF(dark_ ? 0.30 : 0.55);
        band.setColorAt(0.38, QColor(255, 255, 255, 0));
        band.setColorAt(0.50, glass);
        band.setColorAt(0.62, QColor(255, 255, 255, 0));
        p.save();
        p.setClipPath(border);
        p.setPen(Qt::NoPen);
        p.setBrush(band);
        p.drawRect(box);
        p.restore();
      }

      const QRectF content = box.adjusted(kPadX, kPadY, -kPadX, -kPadY);
      // The glyph grows 1.12x and rises 2px with the hover, about its own centre.
      const double iconScale = 1.0 + 0.12 * t;
      const QRectF iconBox(content.center().x() - kIconPx / 2.0,
                           content.top() - 2.0 * t, kIconPx, kIconPx);
      // The shared `image` glyph, stroked inline rather than via support/iconSet
      // (that would drag Qt6::Svg into the headless targets compiling this file).
      // Same 0 0 24 24 geometry + 2px stroke as iconSet.cpp / icons.js; keep in sync.
      p.save();
      p.translate(iconBox.center());
      p.scale(iconScale, iconScale);
      p.translate(-iconBox.width() / 2.0, -iconBox.height() / 2.0);
      p.scale(kIconPx / 24.0, kIconPx / 24.0);
      QPen glyph(ink, 2);
      glyph.setCapStyle(Qt::RoundCap);
      glyph.setJoinStyle(Qt::RoundJoin);
      p.setPen(glyph);
      p.setBrush(Qt::NoBrush);
      p.drawRoundedRect(QRectF(3, 3, 18, 18), 2, 2);
      p.drawEllipse(QPointF(8.5, 8.5), 1.5, 1.5);
      QPolygonF hill;
      hill << QPointF(21, 15) << QPointF(16, 10) << QPointF(5, 21);
      p.drawPolyline(hill);
      p.restore();
      p.setFont(cardFont);
      p.setPen(ink);
      p.drawText(QRectF(content.left(), iconBox.bottom() + kGap, content.width(), textH),
                 Qt::AlignCenter, label);
      return;
    }
    // An image is loaded: the idle card's tooltip must not linger over the artwork.
    if (!toolTip().isEmpty()) setToolTip(QString());
    // Compare: "original" shows the cropped+rotated original alone (a BLANK page
    // keeps its fill + tint, compareBaseImage); the split modes render the edit
    // normally and overlay the original on one half at the end of this function.
    if (filterDirty_) rebuildFilteredImage();
    const QString compare = effectiveCompareMode();
    if (compare == "original") {
      p.drawImage(QRectF(0, 0, image_.width() * scale_, image_.height() * scale_),
                  compareBaseImage());
      return;
    }

    // Draw the raw image when no filter, else the cached filtered copy
    // (rebuilt lazily above).
    const QImage& shown = (imageFilter_ == "none" || filteredImage_.isNull())
                              ? image_
                              : filteredImage_;
    p.drawImage(QRectF(0, 0, image_.width() * scale_, image_.height() * scale_),
                shown);
    // A split compare view is read-only: draw lines/points but with no selection glow or
    // hover/focus rings (a clean picture to compare against).
    const bool hl = compare == "none";
    for (int i = 0; i < static_cast<int>(lines_.size()); ++i)
      drawLineScaled(p, lines_[i], i, scale_, /*highlight=*/hl);
    drawLineScaled(p, currentLine_, -1, scale_, /*highlight=*/hl);

    // Zoom-to-rect rubber band preview.
    if (zoomRectActive_) {
      QPen pen(themePalette(dark_, accentKey_).accent);
      pen.setStyle(Qt::DashLine);
      pen.setWidth(1);
      p.setPen(pen);
      p.setBrush(Qt::NoBrush);
      p.drawRect(QRectF(zoomRectStart_, zoomRectEnd_).normalized());
    }

    // drag-to-create rectangle rubber band (browser drawingApp.js rect-draw
    // overlay). Same dashed-accent style as the zoom band.
    if (rectDrawActive_) {
      QPen pen(themePalette(dark_, accentKey_).accent);
      pen.setStyle(Qt::DashLine);
      pen.setWidth(1);
      p.setPen(pen);
      p.setBrush(Qt::NoBrush);
      p.drawRect(QRectF(rectDrawStart_, rectDrawEnd_).normalized());
    }

    // Hold-to-draw: faded dashed segment from the stroke's anchor to the held
    // cursor + a ghost point — mirrors renderer.js drawHoldPreview. Transient.
    if (holdHasPreview_) {
      const QColor base(QString::fromStdString(
          currentLine_.points.empty() && selectedLine()
              ? selectedLine()->color
              : defColor_.toStdString()));
      const QPointF cur(holdPreview_.x * scale_, holdPreview_.y * scale_);
      if (const core::Point* a = holdAnchor()) {
        QColor line = base; line.setAlphaF(0.45);
        QPen pen(line);
        pen.setStyle(Qt::DashLine);
        pen.setWidthF(std::max(1.0, defThickness_));
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(a->x * scale_, a->y * scale_), cur);
      }
      QColor dot = base; dot.setAlphaF(0.6);
      p.setPen(Qt::NoPen);
      p.setBrush(dot);
      p.drawEllipse(cur, defPointSize_, defPointSize_);
    }

    // Split compare: overlay the untouched original on the original-side half (covering
    // the filtered pixels + annotations there) and draw the movable divider on top.
    if (compare == "vertical" || compare == "horizontal") paintCompareSplit(p, compare, scale_);
  }

  // Paint the original over the "original" half of a split compare view (left for
  // "vertical", top for "horizontal") and draw the draggable divider. Divider metrics are
  // in widget space, so the line keeps a constant on-screen thickness at any zoom.
  void CanvasWidget::paintCompareSplit(QPainter& p, const QString& mode, double scale, bool withDivider) const {
    const double w = image_.width() * scale;
    const double h = image_.height() * scale;
    const double f = std::clamp(compareSplit_, 0.0, 1.0);

    p.save();
    p.setClipRect(mode == "vertical" ? QRectF(0, 0, w * f, h) : QRectF(0, 0, w, h * f));
    // Original — no annotations; a blank page keeps its fill + tint (its colour
    // IS the page), a picture drops the filter too.
    p.drawImage(QRectF(0, 0, w, h), compareBaseImage());
    p.restore();

    if (!withDivider) return;

    p.save();
    p.setClipping(false);
    // Two passes so the white divider + handle read on ANY background (a white line
    // vanishes on the white original side) — a dark casing under the white line and a
    // dark ring around the white knob, mirroring the browser's drop shadow.
    QPen casing(QColor(0, 0, 0, 150), 4);
    casing.setCapStyle(Qt::RoundCap);
    QPen white(QColor(255, 255, 255, 240), 2);
    white.setCapStyle(Qt::RoundCap);
    const double r = 7.0;
    if (mode == "vertical") {
      const double x = w * f;
      p.setPen(casing);
      p.drawLine(QPointF(x, 0), QPointF(x, h));
      p.setPen(white);
      p.drawLine(QPointF(x, 0), QPointF(x, h));
      p.setPen(QPen(QColor(0, 0, 0, 150), 1.5));
      p.setBrush(QColor(255, 255, 255, 240));
      p.drawEllipse(QPointF(x, h / 2), r, r);
    } else {
      const double y = h * f;
      p.setPen(casing);
      p.drawLine(QPointF(0, y), QPointF(w, y));
      p.setPen(white);
      p.drawLine(QPointF(0, y), QPointF(w, y));
      p.setPen(QPen(QColor(0, 0, 0, 150), 1.5));
      p.setBrush(QColor(255, 255, 255, 240));
      p.drawEllipse(QPointF(w / 2, y), r, r);
    }
    p.restore();
  }

  // Which half of a compare view a point lands in (image space). Mirrors the clip
  // rects paintCompareSplit uses: the original covers x < w*f / y < h*f, the edit
  // holds the rest.
  bool CanvasWidget::compareShowsEdited(double imageX, double imageY) const {
    const QString mode = effectiveCompareMode();
    if (mode == QLatin1String("none")) return true;
    const double f = std::clamp(compareSplit_, 0.0, 1.0);
    if (mode == QLatin1String("vertical")) return imageX >= image_.width() * f;
    if (mode == QLatin1String("horizontal")) return imageY >= image_.height() * f;
    return false;  // "original": the edit (and its layout) is nowhere on screen
  }

  // Whether widgetPos is within grab distance (widget px) of the split divider.
  bool CanvasWidget::nearCompareDivider(const QPoint& widgetPos) const {
    if (compareMode_ != "vertical" && compareMode_ != "horizontal") return false;
    const double f = std::clamp(compareSplit_, 0.0, 1.0);
    const double tol = 8.0;
    if (compareMode_ == "vertical")
      return std::abs(widgetPos.x() - image_.width() * scale_ * f) <= tol;
    return std::abs(widgetPos.y() - image_.height() * scale_ * f) <= tol;
  }

  // Flat dispatch; precedence is load-bearing: RightButton -> MiddleButton pan ->
  // Alt+Left drag/pan -> Shift+Left zoom-rect -> Left{Ctrl, rect-draw, select,
  // rect-noop, continuation, close, append}. Alt/Left branches use image space.
  void CanvasWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
      emit contextRequested(event->globalPosition().toPoint());
      return;
    }
    if (image_.isNull()) {
      // Idle: the "＋ Blank image" CARD alone opens the creator — never the empty
      // page around it. While the hint is held back for the clear animation, or
      // before it has been painted, nothing is clickable.
      if (event->button() == Qt::LeftButton && !idleHintHidden_ &&
          idleCardRect_.contains(event->position()))
        emit blankImageRequested();
      return;
    }

    const auto mods = event->modifiers();

    // Compare split divider: plain left-press on the divider starts sliding it. Checked
    // first so it wins over point/line hits underneath, and only when a split mode is the
    // active (not held) view.
    if (event->button() == Qt::LeftButton && mods == Qt::NoModifier && !compareHoldOriginal_ &&
        nearCompareDivider(event->pos())) {
      draggingCompareSplit_ = true;
      setCursor(compareMode_ == "vertical" ? Qt::SplitHCursor : Qt::SplitVCursor);
      return;
    }

    // Compare view is read-only: no drawing/selecting/point-line drags. Only navigation
    // stays — Alt+left / middle-button pan (the divider drag is handled above).
    if (compareReadOnly()) {
      if (event->button() == Qt::MiddleButton ||
          (event->button() == Qt::LeftButton && (mods & Qt::AltModifier))) {
        panning_ = true;
        lastPanPos_ = event->globalPosition().toPoint();
        setCursor(Qt::ClosedHandCursor);
      }
      return;
    }

    // Middle-button always pans (port of drawingApp.js startPan ~757).
    if (event->button() == Qt::MiddleButton) {
      panning_ = true;
      // Track the pan anchor in GLOBAL coords: panBy() scrolls the viewport,
      // sliding this widget under the cursor, so widget-space event->pos() would
      // feed back into the next delta (flicker/jump).
      lastPanPos_ = event->globalPosition().toPoint();
      setCursor(Qt::ClosedHandCursor);
      return;
    }

    if (event->button() == Qt::LeftButton && (mods & Qt::AltModifier)) {
      beginAltDrag(toImageSpace(event->pos().x(), event->pos().y()), mods,
                   event->globalPosition().toPoint());
      return;
    }

    // Ctrl+Shift+left → multi-line select: add/toggle the clicked line. Must precede the plain
    // Shift zoom-rect branch below (which would otherwise swallow Ctrl+Shift). Alt already returned.
    if (event->button() == Qt::LeftButton && (mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier)) {
      toggleLineSelection(toImageSpace(event->pos().x(), event->pos().y()));
      return;
    }

    if (event->button() == Qt::LeftButton && (mods & Qt::ShiftModifier)) {
      beginZoomRect(event->pos());
      return;
    }

    if (event->button() == Qt::LeftButton) {
      const core::Point ip = toImageSpace(event->pos().x(), event->pos().y());
      if (mods & Qt::ControlModifier) {
        if (handleCtrlClick(ip)) return;
        // drawing + Ctrl + no segment -> fall through to handleDrawingClick.
      }
      // Hold-to-draw arms only when not already drawing and with no modifiers.
      // handleDrawingClick still runs first so a quick click keeps selecting; the
      // controller takes over once the press is held near-stationary past the delay.
      const bool eligibleHold = !isDrawing_ && mods == Qt::NoModifier;
      handleDrawingClick(ip, mods, event->pos());
      if (eligibleHold && !isDrawing_) beginHold(event->pos());
    }
  }

  // Alt+left (port of startPan ~721/~764): drag the point, segment, or (with
  // Shift) whole line under the cursor, else pan. Takes precedence over drawing —
  // no points are added while editing/panning. Every path sets state + returns.
  void CanvasWidget::beginAltDrag(const core::Point& ip,
                                  Qt::KeyboardModifiers mods,
                                  const QPoint& globalPos) {
    dragStart_ = ip;
    dragMoved_ = false;

    // Alt+Shift over a line -> whole-line drag (always translates EVERY point).
    if (mods & Qt::ShiftModifier) {
      const int li = core::findLineAt(lines_, ip.x, ip.y, hitRadius(8.0));
      if (li != -1) {
        dragKind_ = DragKind::Line;
        dragLineIdx_ = li;
        dragOrig_ = lines_[li].points;
        // If the grabbed line is part of a multi-selection, snapshot EVERY selected line so the
        // drag translates them all together (whole-line move).
        dragMultiOrig_.clear();
        const auto sel = selectedIndices();
        if (sel.size() >= 2 && std::find(sel.begin(), sel.end(), li) != sel.end())
          for (int i : sel) dragMultiOrig_.push_back({i, lines_[i].points});
        dragPtIdx1_ = dragPtIdx2_ = -1;
        setCursor(Qt::SizeAllCursor);
        return;
      }
    }

    // Priority 1: near a point (in-progress line first) -> drag the point.
    if (auto idx = core::nearestPointInLine(currentLine_.points, ip.x, ip.y,
                                            hitRadius(12.0))) {
      dragKind_ = DragKind::Point;
      dragLineIdx_ = -1;  // in-progress line
      dragPtIdx1_ = *idx;
      setCursor(Qt::SizeAllCursor);
      return;
    }
    if (auto pt = core::findNearestPoint(lines_, ip.x, ip.y, hitRadius(12.0))) {
      dragKind_ = DragKind::Point;
      dragLineIdx_ = pt->lineIdx;
      dragPtIdx1_ = pt->ptIdx;
      setCursor(Qt::SizeAllCursor);
      return;
    }
    // Priority 2: near a segment -> drag that segment.
    if (auto seg = core::findNearestSegment(lines_, ip.x, ip.y, hitRadius(12.0))) {
      dragKind_ = DragKind::Segment;
      dragLineIdx_ = seg->lineIdx;
      dragPtIdx1_ = seg->ptIdx1;
      dragPtIdx2_ = seg->ptIdx2;
      dragOrig_ = lines_[seg->lineIdx].points;
      setCursor(Qt::SizeAllCursor);
      return;
    }
    // Otherwise: pan (drawingApp.js startPan ~797).
    panning_ = true;
    lastPanPos_ = globalPos;  // global: see middle-button note
    setCursor(Qt::ClosedHandCursor);
  }

  // Zoom-to-rect: Shift+left-drag sweeps a rubber band (drawingApp.js
  // startPan shift branch ~746). No points added while sweeping. widgetPos is
  // widget space (NOT image space).
  void CanvasWidget::beginZoomRect(const QPoint& widgetPos) {
    zoomRectActive_ = true;
    zoomRectStart_ = zoomRectEnd_ = widgetPos;
    update();
  }

  // Ctrl+left (drawingApp.js canvasClick ~1187/1239): insert a point onto the
  // nearest segment, else (when not drawing) add a point connected to the selection.
  // Returns false only for drawing+Ctrl+no-segment, which falls through to append.
  bool CanvasWidget::handleCtrlClick(const core::Point& ip) {
    if (auto seg = core::findNearestSegment(lines_, ip.x, ip.y, hitRadius(12.0))) {
      insertPointOnSegment(seg->lineIdx, seg->ptIdx2, ip.x, ip.y);
      // Inserting shifts later indices right by one; keep the continuation
      // tail anchored to the same spot (drawingApp.js ~1193).
      if (seg->lineIdx == continueLineIdx_ &&
          seg->ptIdx2 <= continueInsertIdx_) {
        ++continueInsertIdx_;
      }
      return true;
    }
    if (!isDrawing_) {
      addConnectedPoint(ip.x, ip.y);
      return true;
    }
    // drawing + Ctrl + no segment -> normal append handled by the caller.
    return false;
  }

  // Plain left-click drawing: rect-draw press, select-when-not-drawing, rect-mode
  // no-op, continuation extend/close, the close-shape gate, and the normal point
  // append. widgetPos seeds the rect-draw rubber band (widget space).
  void CanvasWidget::handleDrawingClick(const core::Point& ip,
                                        Qt::KeyboardModifiers mods,
                                        const QPoint& widgetPos) {
    // rect-draw press (drawingApp.js mousedown ~710). While drawing in rect
    // mode with no modifier, begin a drag-to-create rubber band.
    if (isDrawing_ && drawMode_ == DrawMode::Rect && mods == Qt::NoModifier) {
      rectDrawActive_ = true;
      rectDrawStart_ = rectDrawEnd_ = widgetPos;
      update();
      return;
    }

    // when not drawing, a left-click hit-tests + selects a committed line
    // (port of drawingApp.js click-select ~1270) instead of being a no-op.
    if (!isDrawing_) {
      selectLineAt(ip.x, ip.y);
      return;
    }

    // in rect mode, areas are created by dragging, never click-to-add
    // (browser drawingApp.js ~1182).
    if (drawMode_ == DrawMode::Rect) return;

    // Continuation drawing: extend the line being continued (drawingApp.js
    // canvasClick continuation branch ~1201). A click near its first point
    // closes it into a locked area.
    if (continueLineIdx_ >= 0 &&
        continueLineIdx_ < static_cast<int>(lines_.size())) {
      core::Line& line = lines_[continueLineIdx_];
      if (line.points.size() >= 3 &&
          std::hypot(line.points.front().x - ip.x,
                     line.points.front().y - ip.y) <= line.pointSize + 8.0) {
        closeContinuedShape();
        emit changed();
        return;
      }
      insertContinuationPoint(ip, /*advance=*/true);
      update();
      emit changed();
      emit selectionChanged();
      return;
    }

    // Closing an area: with >= 3 points, a click near point[0] closes + locks
    // the shape (mirrors #closeCurrentShape), then stops drawing.
    if (core::shouldCloseShape(currentLine_.points, ip,
                               currentLine_.pointSize)) {
      currentLine_.points.push_back(currentLine_.points.front());
      currentLine_.locked = true;
      stopDrawingMode();
      emit changed();
      return;
    }

    currentLine_.points.push_back(ip);
    selectedPoint_ = static_cast<int>(currentLine_.points.size()) - 1;
    update();
    emit changed();
    emit selectionChanged();
  }

  void CanvasWidget::mouseMoveEvent(QMouseEvent* event) {
    if (image_.isNull()) {
      // Idle: the only thing tracking the cursor is the "＋ Blank image" card's hover fill.
      setIdleCardHover(!idleHintHidden_ && idleCardRect_.contains(event->position()));
      return;
    }
    setIdleCardHover(false);

    // Compare divider drag takes priority over every other gesture.
    if (draggingCompareSplit_) {
      const double f = compareMode_ == "vertical"
                           ? event->pos().x() / (image_.width() * scale_)
                           : event->pos().y() / (image_.height() * scale_);
      setCompareSplit(f);   // clamps + repaints
      emit hoverLeft();     // drop a tooltip left over from before the drag started
      return;
    }

    // Hold-to-draw: while armed/drawing, feed the controller. Moving past the
    // tolerance before the hold fires aborts (a normal click/drag); moving while
    // drawing updates the ghost-line preview. Suppress hover while engaged.
    if (hold_.engaged()) {
      const core::HoldEvent ev =
          hold_.pointerMove(event->pos().x(), event->pos().y(), holdNowMs());
      if (ev.action == core::HoldAction::Abort) {
        stopHold();
      } else if (ev.action == core::HoldAction::Preview) {
        holdPreview_ = core::Point{ev.x / scale_, ev.y / scale_};
        holdHasPreview_ = true;
        update();
      }
      emit hoverLeft();     // drop a tooltip left over from before the hold started
      return;
    }

    // Active Alt-drag gesture (port of drawingApp.js #dragMove ~1701). One of the
    // point/segment/line moves; Shift switches segment/line modes live from the
    // original snapshot so toggling Shift never accumulates.
    if (dragKind_ != DragKind::None) {
      const core::Point ip = toImageSpace(event->pos().x(), event->pos().y());
      const bool shift = bool(event->modifiers() & Qt::ShiftModifier);
      dragMoved_ = true;
      updateDrag(ip, shift);
      update();
      emit hoverLeft();     // drop a tooltip left over from before the drag started
      return;
    }

    if (panning_) {
      // Drag pan: scrollLeft -= dx (Shift = faster; MainWindow applies the speed).
      // Delta in global cursor coords so the scroll we trigger — which moves this
      // widget under the pointer — doesn't feed back into the next sample (jitter).
      const QPoint gp = event->globalPosition().toPoint();
      const QPoint d = gp - lastPanPos_;
      lastPanPos_ = gp;
      emit panBy(d.x(), d.y(), bool(event->modifiers() & Qt::ShiftModifier));
      emit hoverLeft();     // drop a tooltip left over from before the pan started
      return;
    }

    if (zoomRectActive_) {
      zoomRectEnd_ = event->pos();
      update();
      emit hoverLeft();     // drop a tooltip left over from before the drag started
      return;
    }

    // extend the rect-draw rubber band (drawingApp.js mousemove ~815).
    if (rectDrawActive_) {
      rectDrawEnd_ = event->pos();
      update();
      emit hoverLeft();     // drop a tooltip left over from before the drag started
      return;
    }

    // Compare split: show a resize cursor over the movable divider (skip the normal hover
    // cursor + tooltip so the affordance reads clearly).
    if (!compareHoldOriginal_ && event->modifiers() == Qt::NoModifier &&
        (compareMode_ == "vertical" || compareMode_ == "horizontal") &&
        nearCompareDivider(event->pos())) {
      setCursor(compareMode_ == "vertical" ? Qt::SplitHCursor : Qt::SplitVCursor);
      emit hoverLeft();  // the divider is the affordance; drop any tooltip under it
      return;
    }

    const core::Point ip = toImageSpace(event->pos().x(), event->pos().y());

    if (compareReadOnly()) {
      // Comparing is read-only EDITING: no hover ring or cursor affordance, but
      // the coordinate readout and hover tooltips are information and must keep
      // following the cursor (MainWindow drops tooltips the "before" half covers).
      unsetCursor();
      emit hovered(ip.x, ip.y);
      emit hoverDetail(ip.x, ip.y, event->globalPosition().toPoint(),
                       event->modifiers());
      return;
    }

    // Hover ring: track the point under the cursor and repaint when it changes
    // (drawingApp.js canvasMouseMove ~1463 -> renderer point hover ring).
    if (updateHover(ip.x, ip.y)) update();

    // Cursor affordance (drawingApp.js canvasMouseMove ~1476).
    applyHoverCursor(ip, event->modifiers());

    emit hovered(ip.x, ip.y);
    emit hoverDetail(ip.x, ip.y, event->globalPosition().toPoint(),
                     event->modifiers());
  }

  // Active Alt-drag move (port of drawingApp.js #dragMove ~1701): point/segment/
  // line; `shift` switches segment/line modes live from the original snapshot so
  // toggling Shift never accumulates. Caller sets dragMoved_ and repaints.
  void CanvasWidget::updateDrag(const core::Point& ip, bool shift) {
    if (dragKind_ == DragKind::Point) {
      core::Line* line =
          (dragLineIdx_ < 0) ? &currentLine_ : &lines_[dragLineIdx_];
      if (dragPtIdx1_ >= 0 &&
          dragPtIdx1_ < static_cast<int>(line->points.size())) {
        line->points[dragPtIdx1_] = ip;  // snap point to cursor
      }
    } else {
      const double dx = ip.x - dragStart_.x;
      const double dy = ip.y - dragStart_.y;
      // Multi-select whole-line drag: translate EVERY selected line together.
      if (dragKind_ == DragKind::Line && !dragMultiOrig_.empty()) {
        for (auto& entry : dragMultiOrig_) {
          const int li = entry.first;
          if (li < 0 || li >= static_cast<int>(lines_.size())) continue;
          auto& pts = lines_[li].points;
          const auto& orig = entry.second;
          for (std::size_t i = 0; i < pts.size() && i < orig.size(); ++i) {
            pts[i].x = orig[i].x + dx;
            pts[i].y = orig[i].y + dy;
          }
        }
        return;
      }
      // Segment / Line drags translate from the snapshot by (dx, dy). A LINE drag
      // always moves the whole line even when Shift lifts a beat before the mouse —
      // degrading to the grabbed segment would snap the rest back on commit.
      core::Line& line = lines_[dragLineIdx_];
      const bool whole = (dragKind_ == DragKind::Line) ? true : shift;
      if (whole) {
        for (std::size_t i = 0; i < line.points.size(); ++i) {
          line.points[i].x = dragOrig_[i].x + dx;
          line.points[i].y = dragOrig_[i].y + dy;
        }
      } else {
        line.points = dragOrig_;  // reset, then move only the two endpoints
        for (int pi : {dragPtIdx1_, dragPtIdx2_}) {
          line.points[pi].x = dragOrig_[pi].x + dx;
          line.points[pi].y = dragOrig_[pi].y + dy;
        }
      }
    }
  }

  // Alt -> move/grab depending on what's under the cursor; otherwise a pointer
  // over a line. Uses the current hoverPointIdx_ (refreshed by updateHover).
  void CanvasWidget::applyHoverCursor(const core::Point& ip,
                                      Qt::KeyboardModifiers mods) {
    if (mods & Qt::AltModifier) {
      bool overTarget;
      if (mods & Qt::ShiftModifier) {
        overTarget = core::findLineAt(lines_, ip.x, ip.y, hitRadius(8.0)) != -1;
      } else {
        overTarget = (hoverPointIdx_ >= 0) ||
                     core::findNearestSegment(lines_, ip.x, ip.y, hitRadius(12.0))
                         .has_value();
      }
      setCursor(overTarget ? Qt::SizeAllCursor : Qt::OpenHandCursor);
    } else if (!isDrawing_) {
      // Crosshair says "click to place a point" (browser parity: layout.css's unconditional
      // `cursor: crosshair` whenever the canvas is drawable) — a plain arrow gave no such
      // affordance (user report). A line still gets its own pointing-hand hint.
      setCursor(core::findLineAt(lines_, ip.x, ip.y, hitRadius(8.0)) != -1
                    ? Qt::PointingHandCursor
                    : Qt::CrossCursor);
    } else {
      setCursor(Qt::CrossCursor);   // actively drawing: the aim, not a plain arrow
    }
  }

  // App-wide filter: on a modifier key press/release with the cursor over the
  // canvas, re-apply hover so tooltip + cursor update without a mouse move
  // (browser parity: the same handlers run on keydown/keyup).
  bool CanvasWidget::eventFilter(QObject* watched, QEvent* event) {
    const QEvent::Type t = event->type();
    if (t == QEvent::KeyPress || t == QEvent::KeyRelease) {
      auto* ke = static_cast<QKeyEvent*>(event);
      if (!ke->isAutoRepeat()) {
        const int key = ke->key();
        if (key == Qt::Key_Shift
            || key == Qt::Key_Control
            || key == Qt::Key_Alt
            || key == Qt::Key_AltGr
            || key == Qt::Key_Meta) {
          refreshHoverForModifiers();
        }
      }
    }
    return QWidget::eventFilter(watched, event);
  }

  void CanvasWidget::refreshHoverForModifiers() {
    // Only while idly hovering the canvas — never mid-gesture.
    if (image_.isNull() || !underMouse()) return;
    if (panning_ || rectDrawActive_ || zoomRectActive_ ||
        dragKind_ != DragKind::None) {
      return;
    }
    const QPoint wp = mapFromGlobal(QCursor::pos());
    if (!rect().contains(wp)) return;

    const core::Point ip = toImageSpace(wp.x(), wp.y());
    // queryKeyboardModifiers() reports the live physical state, which (unlike the
    // key event's own modifiers()) already includes the key being pressed.
    const Qt::KeyboardModifiers mods = QGuiApplication::queryKeyboardModifiers();
    // Same split as the mouse path: a compare view keeps the readout + tooltip but
    // no hover ring or edit cursor.
    if (compareReadOnly()) {
      unsetCursor();
    } else {
      if (updateHover(ip.x, ip.y)) update();
      applyHoverCursor(ip, mods);
    }
    emit hovered(ip.x, ip.y);
    emit hoverDetail(ip.x, ip.y, QCursor::pos(), mods, /*immediate=*/true);
  }

  // Hover the idle card the way the browser does (.idle-create-btn transitions colour,
  // lift and shadow over 0.2s) rather than snapping between two states.
  void CanvasWidget::setIdleCardHover(bool on) {
    if (idleCardHover_ == on) return;
    idleCardHover_ = on;
    // The hand belongs to the button, so it appears exactly where the click works.
    if (on) setCursor(Qt::PointingHandCursor);
    else unsetCursor();
    if (!idleCardAnim_) {
      idleCardAnim_ = new QVariantAnimation(this);
      idleCardAnim_->setDuration(200);
      idleCardAnim_->setEasingCurve(QEasingCurve::OutCubic);
      connect(idleCardAnim_, &QVariantAnimation::valueChanged, this,
              [this](const QVariant& v) { idleCardHoverT_ = v.toDouble(); update(); });
    }
    idleCardAnim_->stop();
    // Start from wherever the previous run got to, so a quick in-out doesn't jump.
    idleCardAnim_->setStartValue(idleCardHoverT_);
    idleCardAnim_->setEndValue(on ? 1.0 : 0.0);
    idleCardAnim_->start();

    // …and the glass sweep the browser gives every button on hover-enter
    // (layout.css ui-shimmer, 0.75s: a light band crossing from -135% to 135%).
    if (!on) return;
    if (!idleShimmerAnim_) {
      idleShimmerAnim_ = new QVariantAnimation(this);
      idleShimmerAnim_->setDuration(750);
      idleShimmerAnim_->setStartValue(0.0);
      idleShimmerAnim_->setEndValue(1.0);
      idleShimmerAnim_->setEasingCurve(QEasingCurve::InOutQuad);
      connect(idleShimmerAnim_, &QVariantAnimation::valueChanged, this,
              [this](const QVariant& v) { idleShimmerT_ = v.toDouble(); update(); });
      connect(idleShimmerAnim_, &QVariantAnimation::finished, this,
              [this] { idleShimmerT_ = -1.0; update(); });
    }
    idleShimmerAnim_->stop();
    idleShimmerT_ = 0.0;
    idleShimmerAnim_->start();
  }

  void CanvasWidget::leaveEvent(QEvent* event) {
    emit hoverLeft();
    emit canvasLeft();
    if (hoverLineIdx_ != -1 || hoverPointIdx_ != -1 || hoverOverLineIdx_ != -1) {
      hoverLineIdx_ = -1;
      hoverPointIdx_ = -1;
      hoverOverLineIdx_ = -1;
      emit canvasHoverChanged(-1, -1, -1);   // panel row tints clear too
      update();
    }
    setIdleCardHover(false);
    QWidget::leaveEvent(event);
  }

  void CanvasWidget::mouseReleaseEvent(QMouseEvent* event) {
    // Finish a compare-divider drag.
    if (draggingCompareSplit_) {
      draggingCompareSplit_ = false;
      unsetCursor();
      return;
    }

    // Finish a hold-to-draw gesture. Releasing after a stroke commits the line and
    // exits drawing; releasing a quick/aborted hold just tears down (selection
    // already happened on press in handleDrawingClick).
    if (holdTimer_.isActive() || hold_.engaged()) {
      holdTimer_.stop();
      const core::HoldEvent ev = hold_.pointerUp(holdNowMs());
      if (ev.action == core::HoldAction::Commit) {
        holdCommit();
      } else if (holdHasPreview_) {
        holdHasPreview_ = false;
        update();
      }
      return;
    }

    // Finish an Alt-drag gesture (drawingApp.js mouseup ~925-949). Commit one
    // undo step only when a committed line actually moved; an in-progress-line
    // point edit just refreshes the panel.
    if (dragKind_ != DragKind::None) {
      const bool moved = dragMoved_;
      const bool committed = moved && dragLineIdx_ >= 0;
      dragKind_ = DragKind::None;
      dragLineIdx_ = dragPtIdx1_ = dragPtIdx2_ = -1;
      dragOrig_.clear();
      dragMultiOrig_.clear();
      unsetCursor();
      update();
      if (committed) commitHistory();   // emits changed()
      if (moved) emit selectionChanged();
      return;
    }
    if (panning_) {
      panning_ = false;
      unsetCursor();
      return;
    }
    if (zoomRectActive_) {
      zoomRectActive_ = false;
      // Convert the swept rubber band to image space and emit. Only act on a
      // rect bigger than 4x4 image px (drawingApp.js mouseup ~882).
      const core::Point a = toImageSpace(zoomRectStart_.x(), zoomRectStart_.y());
      const core::Point b = toImageSpace(zoomRectEnd_.x(), zoomRectEnd_.y());
      const double x1 = std::min(a.x, b.x);
      const double y1 = std::min(a.y, b.y);
      const double w = std::abs(b.x - a.x);
      const double h = std::abs(b.y - a.y);
      update();
      if (w > 4.0 && h > 4.0) emit zoomToRect(QRectF(x1, y1, w, h));
      return;
    }
    // commit the drag-to-create rectangle (drawingApp.js mouseup ~861). Only
    // act when the swept box exceeds 3 image px in both axes.
    if (rectDrawActive_) {
      rectDrawActive_ = false;
      const core::Point a = toImageSpace(rectDrawStart_.x(), rectDrawStart_.y());
      const core::Point b = toImageSpace(rectDrawEnd_.x(), rectDrawEnd_.y());
      if (std::abs(b.x - a.x) > 3.0 && std::abs(b.y - a.y) > 3.0) {
        createRect(a.x, a.y, b.x, b.y);
      }
      update();
      return;
    }
    QWidget::mouseReleaseEvent(event);
  }

  void CanvasWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    // Alt+left or middle double-click = fit to window (drawingApp.js resetZoom
    // ~964). A double-click also fires a press first, which set panning_; clear it.
    const bool altLeft = event->button() == Qt::LeftButton &&
                         (event->modifiers() & Qt::AltModifier);
    if (altLeft || event->button() == Qt::MiddleButton) {
      panning_ = false;
      unsetCursor();
      emit fitRequested();
      return;
    }
    // Plain left double-click on a line erases it (drawingApp.js canvasDblClick).
    if (event->button() == Qt::LeftButton && !isDrawing_ && !compareReadOnly()) {
      const QPoint pos = event->position().toPoint();
      const core::Point ip = toImageSpace(pos.x(), pos.y());
      const int idx = core::findLineAt(lines_, ip.x, ip.y, hitRadius(8.0));
      if (idx != -1) {
        panning_ = false;   // the press that opened this double-click armed it
        unsetCursor();
        lines_.erase(lines_.begin() + idx);
        clearHoverCache();   // indices shifted
        selectedLines_.clear();
        if (selectedLineIdx_ == idx) selectedLineIdx_ = -1;
        else if (selectedLineIdx_ > idx) --selectedLineIdx_;
        selectedPoint_ = -1;
        commitHistory();
        update();
        emit changed();
        emit selectionChanged();
        return;
      }
    }
    QWidget::mouseDoubleClickEvent(event);
  }

  void CanvasWidget::wheelEvent(QWheelEvent* event) {
    // drawingApp.js wheel (~655). Use whichever axis carries the delta: with a
    // modifier held, X11/GNOME often reports the wheel on angleDelta().x() with .y()==0.
    const auto mods = event->modifiers();
    const QPoint d = event->angleDelta();
    const int delta = d.y() != 0 ? d.y() : d.x();
    if (delta == 0) {
      event->ignore();
      return;
    }

    // Alt+wheel (no Ctrl): adjust thickness of the line under the cursor; wheel-up
    // thickens (drawingApp.js #adjustThicknessAtCursor ~1810). Ctrl+wheel still zooms.
    if ((mods & Qt::AltModifier) && !(mods & Qt::ControlModifier)) {
      const QPoint wp = event->position().toPoint();
      const core::Point ip = toImageSpace(wp.x(), wp.y());
      adjustThicknessAtCursor(ip.x, ip.y, delta > 0 ? 1 : -1);
      event->accept();
      return;
    }

    // Ctrl+Shift+wheel with a selected line: rotate it about its center (or the
    // focused point) by 3 deg/tick (drawingApp.js wheel ~666). With no selection
    // it falls through to a fast (Shift) zoom.
    if ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier) &&
        selectionCount() >= 1) {
      rotateSelectedLine((delta > 0 ? 1.0 : -1.0) * (M_PI / 60.0));
      event->accept();
      return;
    }

    // Ctrl+wheel: zoom toward the cursor. Shift triples the step.
    if (mods & Qt::ControlModifier) {
      const bool fast = bool(mods & Qt::ShiftModifier);
      emit zoomAtCursor(delta > 0 ? 1 : -1, event->position().toPoint(), fast);
      event->accept();
      return;
    }

    // Plain wheel: let the scroll area handle it.
    event->ignore();
  }

  // Trackpad pinch (macOS/Wayland) arrives as QEvent::NativeGesture — QWidget has
  // no dedicated virtual for it. value() is the incremental scale delta per event;
  // multiply the current scale by (1 + delta), anchored at the cursor.
  bool CanvasWidget::event(QEvent* e) {
    if (e->type() == QEvent::NativeGesture) {
      auto* g = static_cast<QNativeGestureEvent*>(e);
      if (g->gestureType() == Qt::ZoomNativeGesture) {
        const double factor = 1.0 + g->value();
        if (factor > 0.0 && factor != 1.0)
          emit zoomByFactorAt(factor, g->position().toPoint());
        g->accept();
        return true;
      }
    }
    return QWidget::event(e);
  }

  // ── render-to-image + image accessors + loadFromImage ──

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
        drawLineScaled(p, lines_[i], i, 1.0, /*highlight=*/false);
      drawLineScaled(p, currentLine_, -1, 1.0, /*highlight=*/false);
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

  // ── selected-line mutators + delete (port of applySelectionChange ~1674
  // and canvasDblClick delete ~1515) ──

  void CanvasWidget::mutateSelectedLine(const std::function<void(core::Line&)>& set) {
    if (compareReadOnly()) return;   // read-only compare view (selection-panel edits)
    core::Line* line = selectedLine();
    if (!line) return;
    set(*line);
    commitHistory();
    update();
    emit selectionChanged();
  }

  void CanvasWidget::setSelectedLineColor(const QString& color) {
    mutateSelectedLine([&](core::Line& line) {
      // Recolouring the stroke must never recolour the points: a line still on the
      // inherit fallback ('' pointColor) pins its rendered colour first (browser parity).
      if (line.pointColor.empty()) line.pointColor = line.color;
      line.color = color.toStdString();
    });
  }

  void CanvasWidget::setSelectedLineThickness(double thickness) {
    mutateSelectedLine([&](core::Line& line) { line.thickness = thickness; });
  }

  void CanvasWidget::setSelectedLinePointSize(double pointSize) {
    mutateSelectedLine([&](core::Line& line) { line.pointSize = pointSize; });
  }

  void CanvasWidget::setSelectedLinePointColor(const QString& pointColor) {
    mutateSelectedLine(
        [&](core::Line& line) { line.pointColor = pointColor.toStdString(); });
  }

  void CanvasWidget::setSelectedLineStyle(const QString& style) {
    mutateSelectedLine([&](core::Line& line) { line.style = style.toStdString(); });
  }

  void CanvasWidget::setSelectedLineFill(const QString& fillColor) {
    mutateSelectedLine([&](core::Line& line) { line.fillColor = fillColor.toStdString(); });
  }

  // Deletes the WHOLE selection (browser parity: removeSelectedLines): erase from
  // the highest index down so lower indices stay valid, and commit ONE history
  // entry so a single undo restores the batch.
  void CanvasWidget::deleteSelectedLine() {
    if (compareReadOnly()) return;   // read-only compare view
    std::vector<int> sel = selectedIndices();
    if (sel.empty()) return;
    std::sort(sel.begin(), sel.end(), std::greater<int>());
    for (int idx : sel) {
      if (idx < 0 || idx >= static_cast<int>(lines_.size())) continue;
      lines_.erase(lines_.begin() + idx);
    }
    clearHoverCache();   // indices shifted
    selectedLines_.clear();
    selectedLineIdx_ = -1;
    selectedPoint_ = -1;
    commitHistory();
    update();
    emit changed();
    emit selectionChanged();
  }

  // ── hold-to-draw (alternative flow; port of browser holdDraw.js) ──

  void CanvasWidget::setHoldDrawDelay(int ms) {
    holdDelayMs_ = std::max(100, std::min(3000, ms));
    hold_.setHoldDelay(holdDelayMs_);
  }

  double CanvasWidget::holdNowMs() const {
    return static_cast<double>(holdClock_.elapsed());
  }

  void CanvasWidget::beginHold(const QPoint& widgetPos) {
    holdPressPos_ = widgetPos;
    hold_.pointerDown(widgetPos.x(), widgetPos.y(), holdNowMs());
    holdTimer_.start();
  }

  void CanvasWidget::stopHold() {
    holdTimer_.stop();
    hold_.cancel();
    if (holdHasPreview_) {
      holdHasPreview_ = false;
      update();
    }
  }

  void CanvasWidget::handleHoldTick() {
    const core::HoldEvent ev = hold_.tick(holdNowMs());
    if (ev.action == core::HoldAction::Start) holdStart(ev.x, ev.y);
    else if (ev.action == core::HoldAction::Drop) holdDrop(ev.x, ev.y);
  }

  // Hold completed → auto-enter drawing and seed the stroke. The target under the
  // press decides: existing point → continue that line; line body → insert a point
  // there then continue; empty → fresh line.
  void CanvasWidget::holdStart(double widgetX, double widgetY) {
    const core::Point ip{widgetX / scale_, widgetY / scale_};
    const core::HoldTarget t = core::holdDrawTarget(lines_, ip.x, ip.y);
    holdPrepend_ = false;
    if (t.kind == core::HoldTargetKind::ContinuePoint) {
      selectedLineIdx_ = t.lineIdx;
      selectedPoint_ = t.ptIdx;
      startDrawingMode();
      // Holding the FIRST point extends the line backward: prepend new points
      // before it (index 0) instead of inserting after it as the second point.
      if (t.ptIdx == 0) { holdPrepend_ = true; continueInsertIdx_ = 0; }
    } else if (t.kind == core::HoldTargetKind::InsertSegment) {
      insertPointOnSegment(t.lineIdx, t.ptIdx2, ip.x, ip.y);
      startDrawingMode();
    } else {
      selectedLineIdx_ = -1;
      selectedPoint_ = -1;
      startDrawingMode();
      currentLine_.points.push_back(ip);
    }
    holdPreview_ = ip;
    holdHasPreview_ = true;
    update();
    emit changed();
    emit selectionChanged();
  }

  // Insert ip into the continued line at the clamped insert cursor + select it. Prepend
  // mode keeps inserting at the head; forward mode advances so points keep appending.
  void CanvasWidget::insertContinuationPoint(const core::Point& ip, bool advance) {
    core::Line& line = lines_[continueLineIdx_];
    const int at = std::max(
        0, std::min(continueInsertIdx_, static_cast<int>(line.points.size())));
    line.points.insert(line.points.begin() + at, ip);
    selectedPoint_ = at;
    if (advance) continueInsertIdx_ = at + 1;
  }

  // Dwell completed → drop a point (extends the in-progress / continued line).
  void CanvasWidget::holdDrop(double widgetX, double widgetY) {
    const core::Point ip{widgetX / scale_, widgetY / scale_};
    if (continueLineIdx_ >= 0 &&
        continueLineIdx_ < static_cast<int>(lines_.size())) {
      insertContinuationPoint(ip, /*advance=*/!holdPrepend_);
    } else {
      currentLine_.points.push_back(ip);
    }
    holdPreview_ = ip;
    holdHasPreview_ = true;
    update();
    emit changed();
    emit selectionChanged();
  }

  // Release after a hold stroke → commit the line and disable drawing again.
  void CanvasWidget::holdCommit() {
    holdHasPreview_ = false;
    holdPrepend_ = false;
    if (isDrawing_) stopDrawingMode();  // commits + emits drawingModeChanged(false)
    update();
  }

  // Origin of the hold-draw preview line: the tail of the in-progress line, or the
  // current insertion tail of the line being extended. nullptr = nothing to anchor.
  const core::Point* CanvasWidget::holdAnchor() const {
    if (!currentLine_.points.empty()) return &currentLine_.points.back();
    if (continueLineIdx_ >= 0 &&
        continueLineIdx_ < static_cast<int>(lines_.size())) {
      const std::vector<core::Point>& pts = lines_[continueLineIdx_].points;
      if (pts.empty()) return nullptr;
      // Prepend: the next point connects to the current head (continueInsertIdx_);
      // forward: it connects to the point just before the insertion tail.
      const int idx = holdPrepend_ ? continueInsertIdx_ : continueInsertIdx_ - 1;
      if (idx >= 0 && idx < static_cast<int>(pts.size())) return &pts[idx];
      return &pts.back();
    }
    return nullptr;
  }

  // ── interactive editing helpers (port of drawingApp.js) ──

  // Refresh the hovered point under the cursor; returns true when it changed (so
  // the caller repaints). Checks the in-progress line first (lineIdx -1), then
  // committed lines. Port of #findNearestPointWithIdx + the hoverPt bookkeeping.
  bool CanvasWidget::updateHover(double imageX, double imageY) {
    int li = -1;
    int pi = -1;
    if (auto idx = core::nearestPointInLine(currentLine_.points, imageX, imageY,
                                            hitRadius(12.0))) {
      li = -1;
      pi = *idx;
    }
    if (pi < 0) {
      if (auto pt = core::findNearestPoint(lines_, imageX, imageY, hitRadius(12.0))) {
        li = pt->lineIdx;
        pi = pt->ptIdx;
      }
    }
    // The committed LINE under the cursor (a point hit names its line, else a stroke
    // hit) — tints the panel's Lines-list row, the reverse of setListHoverLine.
    const int over =
        (li >= 0) ? li : core::findLineAt(lines_, imageX, imageY, hitRadius(8.0));
    if (li == hoverLineIdx_ && pi == hoverPointIdx_ && over == hoverOverLineIdx_)
      return false;
    hoverLineIdx_ = li;
    hoverPointIdx_ = pi;
    hoverOverLineIdx_ = over;
    emit canvasHoverChanged(li, pi, over);
    return true;
  }

  int CanvasWidget::panelLineIdx() const {
    const core::Line* l = panelLine();
    if (!l || l == &currentLine_ || lines_.empty()) return -1;
    return static_cast<int>(l - lines_.data());
  }

  void CanvasWidget::setListHoverPoint(int ptIdx) {
    const core::Line* l = panelLine();
    const int n = l ? static_cast<int>(l->points.size()) : 0;
    const int i = (ptIdx >= 0 && ptIdx < n) ? ptIdx : -1;
    if (i == listHoverPointIdx_) return;
    listHoverPointIdx_ = i;
    update();
  }

  void CanvasWidget::setListHoverLine(int lineIdx) {
    const int i =
        (lineIdx >= 0 && lineIdx < static_cast<int>(lines_.size())) ? lineIdx : -1;
    if (i == listHoverLineIdx_) return;
    listHoverLineIdx_ = i;
    update();
  }

  void CanvasWidget::clearHoverCache() {
    if (hoverLineIdx_ == -1 && hoverPointIdx_ == -1 && hoverOverLineIdx_ == -1 &&
        listHoverPointIdx_ == -1 && listHoverLineIdx_ == -1) {
      return;
    }
    hoverLineIdx_ = -1;
    hoverPointIdx_ = -1;
    hoverOverLineIdx_ = -1;
    listHoverPointIdx_ = -1;
    listHoverLineIdx_ = -1;
    emit canvasHoverChanged(-1, -1, -1);
  }

  // Alt+wheel: bump the thickness of the line under the cursor by ±1 (clamped
  // 1–20). Prefers the hovered point's line, else the segment under the cursor.
  // Port of drawingApp.js #adjustThicknessAtCursor (~1810).
  void CanvasWidget::adjustThicknessAtCursor(double imageX, double imageY,
                                             int dir) {
    int lineIdx = -1;
    if (auto pt = core::findNearestPoint(lines_, imageX, imageY, hitRadius(12.0))) {
      lineIdx = pt->lineIdx;
    } else {
      lineIdx = core::findLineAt(lines_, imageX, imageY, hitRadius(8.0));
    }
    if (lineIdx < 0 || lineIdx >= static_cast<int>(lines_.size())) return;

    core::Line& line = lines_[lineIdx];
    const double newT = std::max(1.0, std::min(20.0, line.thickness + dir));
    if (newT == line.thickness) return;
    line.thickness = newT;
    update();
    emit selectionChanged();  // refresh the panel if this line is selected
    scheduleEditCommit();
  }

  // Apply `op` (rotate/flip) to the selection about its pivot: ≥2 selected → the
  // combined bbox centre; 1 → the focused point, else that line's bbox centre.
  // Shared scaffold so rotate and flip can't drift (no new core op).
  void CanvasWidget::transformSelection(
      const std::function<void(std::vector<core::Point>&, double, double)>& op) {
    if (compareReadOnly()) return;   // read-only compare view
    const auto sel = selectedIndices();
    if (sel.size() >= 2) {
      std::vector<core::Point> all;
      for (int i : sel)
        for (const auto& p : lines_[i].points) all.push_back(p);
      if (all.size() < 2) return;
      const core::Point c = core::boundingBoxCenter(all);
      for (int i : sel) op(lines_[i].points, c.x, c.y);
    } else {
      core::Line* line = selectedLine();
      if (!line || line->points.size() < 2) return;
      double cx;
      double cy;
      if (selectedPoint_ >= 0 &&
          selectedPoint_ < static_cast<int>(line->points.size())) {
        cx = line->points[selectedPoint_].x;
        cy = line->points[selectedPoint_].y;
      } else {
        const core::Point c = core::boundingBoxCenter(line->points);
        cx = c.x;
        cy = c.y;
      }
      op(line->points, cx, cy);
    }
    update();
    emit selectionChanged();
    scheduleEditCommit();
  }

  // Ctrl+Shift+wheel / Alt+R+←/→: rotate the selected line(s) about the selection pivot.
  // Port of #rotateSelectedLine (~1834).
  void CanvasWidget::rotateSelectedLine(double angleRad) {
    transformSelection([angleRad](std::vector<core::Point>& pts, double cx, double cy) {
      core::rotatePoints(pts, cx, cy, angleRad);
    });
  }

  // Alt+Shift+↑/↓: mirror the selected line(s) about the selection pivot (horizontal == left↔right,
  // x' = 2*cx - x; otherwise top↔bottom, y' = 2*cy - y) — core::flipPoints in place of the rotate.
  void CanvasWidget::flipSelectedLine(bool horizontal) {
    transformSelection([horizontal](std::vector<core::Point>& pts, double cx, double cy) {
      core::flipPoints(pts, horizontal, cx, cy);
    });
  }

  // Translate every selected line by (dx, dy) image-space px — the arrow-key nudge (mirror of
  // the browser drawingApp.js nudgeSelected). Debounced commit, like the wheel rotate, so a
  // burst of key-repeats collapses into one undo step.
  void CanvasWidget::nudgeSelected(double dx, double dy) {
    if (compareReadOnly()) return;   // read-only compare view
    if (dx == 0.0 && dy == 0.0) return;
    const auto sel = selectedIndices();
    if (sel.empty()) return;
    for (int i : sel) {
      if (i < 0 || i >= static_cast<int>(lines_.size())) continue;
      for (auto& p : lines_[i].points) { p.x += dx; p.y += dy; }
    }
    update();
    emit selectionChanged();
    scheduleEditCommit();
  }

  // (Re)start the debounce so a burst of wheel ticks collapses into one undo
  // step. The single-shot timer fires commitHistory() once the wheel goes quiet.
  void CanvasWidget::scheduleEditCommit() { editCommitTimer_.start(); }

  // Insert a new point into an existing line between two of its points, select
  // that line and focus the new point. Port of drawingApp.js #insertPointOnSegment
  // (~1335).
  void CanvasWidget::insertPointOnSegment(int lineIdx, int insertIdx, double x,
                                          double y) {
    if (lineIdx < 0 || lineIdx >= static_cast<int>(lines_.size())) return;
    core::Line& line = lines_[lineIdx];
    const int at = std::max(
        0, std::min(insertIdx, static_cast<int>(line.points.size())));
    line.points.insert(line.points.begin() + at, core::Point{x, y});
    selectedLineIdx_ = lineIdx;
    selectedPoint_ = at;
    commitHistory();  // emits changed()
    update();
    emit selectionChanged();
  }

  // Add a point connected to the current selection (after the focused point, else
  // at the line's tail), or start a new single-point line when nothing is
  // selected. Port of drawingApp.js #addConnectedPoint (~1352).
  void CanvasWidget::addConnectedPoint(double x, double y) {
    if (selectedLineIdx_ >= 0 &&
        selectedLineIdx_ < static_cast<int>(lines_.size())) {
      core::Line& line = lines_[selectedLineIdx_];
      const int insertIdx = (selectedPoint_ >= 0)
                                ? selectedPoint_ + 1
                                : static_cast<int>(line.points.size());
      const int at = std::max(
          0, std::min(insertIdx, static_cast<int>(line.points.size())));
      line.points.insert(line.points.begin() + at, core::Point{x, y});
      selectedPoint_ = at;
      commitHistory();
      update();
      emit selectionChanged();
      return;
    }
    core::Line nl;
    nl.points = {{x, y}};
    nl.color = defColor_.toStdString();
    nl.pointColor =
        (defPointColor_.isEmpty() ? defColor_ : defPointColor_).toStdString();
    nl.thickness = defThickness_;
    nl.pointSize = defPointSize_;
    nl.style = defStyle_.toStdString();
    lines_.push_back(nl);
    selectedLineIdx_ = static_cast<int>(lines_.size()) - 1;
    selectedPoint_ = 0;
    commitHistory();
    update();
    emit selectionChanged();
  }

  // Close the line being extended into a locked area, keep it selected, and leave
  // drawing mode. Port of drawingApp.js #closeContinuedShape (~1310).
  void CanvasWidget::closeContinuedShape() {
    const int li = continueLineIdx_;
    if (li < 0 || li >= static_cast<int>(lines_.size())) return;
    core::Line& line = lines_[li];
    if (line.points.size() < 3) return;
    line.points.push_back(line.points.front());
    line.locked = true;
    if (line.fillColor.empty()) line.fillColor = "transparent";
    continueLineIdx_ = -1;
    continueInsertIdx_ = -1;
    currentLine_ = core::Line{};
    applyDefaultsToCurrent();
    isDrawing_ = false;
    selectedLineIdx_ = li;
    selectedPoint_ = -1;
    commitHistory();
    update();
    emit drawingModeChanged(false);
    emit selectionChanged();
  }

}

#include "CanvasScene.hpp"
#include "imageTurn.hpp"

#include <QStringList>

#include <algorithm>

// The scene's look, its filter and compare state, and the lines' defaults. The document edits are
// in sceneDocument.cpp, the paint path in scenePaint.cpp, sceneFlights.cpp and sceneRender.cpp.

namespace stencil::gui {

  CanvasScene::CanvasScene() { applyDefaultsToCurrent(); }

  CanvasScene::~CanvasScene() = default;

  // crop (shared cropGeometry; mirrors browser DrawingApp)
  void CanvasScene::setPageCm(double widthCm, double heightCm) {
    if (widthCm > 0) pageWidthCm = widthCm;
    if (heightCm > 0) pageHeightCm = heightCm;
  }

  // Returns the original untouched at 0.
  QImage CanvasScene::effectiveOriginalImage() const {
    return model::turn(originalImage, rotationQuarters);
  }

  core::Lines CanvasScene::allLines() const {
    core::Lines all = lines;
    if (!currentLine.points.empty()) all.push_back(currentLine);
    return all;
  }

  void CanvasScene::setDefaults(const QString& color, double thickness,
                                double pointSize, const QString& style,
                                const QString& pointColor) {
    defColor = color;
    defThickness = thickness;
    defPointSize = pointSize;
    defStyle = style;
    defPointColor = pointColor;
    if (currentLine.points.empty()) applyDefaultsToCurrent();
    sceneChanged();
  }

  void CanvasScene::applyDefaultsToCurrent() {
    currentLine.color = defColor.toStdString();
    currentLine.thickness = defThickness;
    currentLine.pointSize = defPointSize;
    currentLine.style = defStyle.toStdString();
    // Resolved AT DRAW TIME (empty setting → the current line colour) so a later
    // line-colour change never recolours already-drawn points (browser parity).
    currentLine.pointColor =
        (defPointColor.isEmpty() ? defColor : defPointColor).toStdString();
  }

  void CanvasScene::setShowPoints(bool on) {
    showPoints = on;
    sceneChanged();
  }
  void CanvasScene::setShowLines(bool on) {
    showLines = on;
    sceneChanged();
  }
  void CanvasScene::setDark(bool dark) {
    this->dark = dark;
    sceneChanged();
  }

  void CanvasScene::setAccent(const QString& accentKey) {
    this->accentKey = accentKey;
    sceneChanged();
  }

  void CanvasScene::setHighlightColors(const QColor& selGlow, const QColor& hoverRing,
                                       const QColor& focusRing) {
    if (selGlow.isValid()) this->selGlow = selGlow;
    if (hoverRing.isValid()) this->hoverRing = hoverRing;
    if (focusRing.isValid()) this->focusRing = focusRing;
    sceneChanged();
  }

  // image filters (port of browser/js/core/draw/renderer.js
  // drawImageWithFilter ~9 + #applyTintFilter ~164)
  void CanvasScene::setFilter(const QString& mode) {
    imageFilter = mode;
    filterMode = core::filterModeFromString(mode.toStdString());
    filterDirty = true;
    sceneChanged();
  }

  void CanvasScene::setFilterColor(const QColor& tint) {
    filterColor = tint;
    filterDirty = true;
    sceneChanged();
  }

  void CanvasScene::setImageFilter(const QString& mode, const QColor& tint) {
    imageFilter = mode;
    filterMode = core::filterModeFromString(mode.toStdString());
    filterColor = tint;
    filterDirty = true;
    sceneChanged();  // single repaint for both
  }

  // Measured against the step on screen, not the picture: a hover preview already shows the pick.
  bool CanvasScene::commitFilter(const QString& mode, const QColor& tint) {
    setImageFilter(mode, tint);
    if (originalImage.isNull() || (mode == stepFilter && tint.name() == stepTint.name())) return false;
    pushStep();
    return true;
  }

  // compare view (port of browser DrawingApp / renderer.js)
  namespace {
    // CompareMode's keys, in its order: the toolbar and the export speak these.
    const QStringList& compareKeys() {
      static const QStringList keys{"none", "original", "vertical", "horizontal"};
      return keys;
    }
  }  // namespace

  void CanvasScene::setCompareMode(const QString& mode) {
    const int at = compareKeys().indexOf(mode);
    if (at < 0 || compareMode == CompareMode(at)) return;
    compareMode = CompareMode(at);
    sceneChanged();
  }

  QString CanvasScene::getCompareMode() const { return compareKeys().at(int(compareMode)); }

  void CanvasScene::setCompareSplit(double fraction) {
    compareSplit = std::clamp(fraction, 0.02, 0.98);
    if (isSplitCompare()) sceneChanged();
  }

  void CanvasScene::setCompareHoldOriginal(bool on) {
    if (compareHoldOriginal == on) return;
    compareHoldOriginal = on;
    sceneChanged();
  }

  // Loads reset this to false; the owner re-marks blanks right after creating,
  // recolouring, or reopening one.
  void CanvasScene::setBlankPage(bool on) {
    if (blankPage == on) return;
    blankPage = on;
    sceneChanged();
  }

  // The original covers x < w*f / y < h*f (the clip rects paintCompareSplit uses).
  bool CanvasScene::compareShowsEdited(double imageX, double imageY) const {
    const CompareMode mode = effectiveCompareMode();
    if (mode == CompareMode::NONE) return true;
    const double f = std::clamp(compareSplit, 0.0, 1.0);
    if (mode == CompareMode::VERTICAL) return imageX >= image.width() * f;
    if (mode == CompareMode::HORIZONTAL) return imageY >= image.height() * f;
    return false;  // "original": the edit (and its layout) is nowhere on screen
  }

}  // namespace stencil::gui

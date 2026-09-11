#include "mainWindow.hpp"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include "mainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "chatPlanTarget.hpp"
#include "logoHoverFx.hpp"
#include "planExecutor.hpp"
#include "canvasWidget.hpp"
#include "incognitoOverlay.hpp"
#include "guiHelpers.hpp"
#include "projectsDialog.hpp"
#include "remoteSyncController.hpp"
#include "serverClient.hpp"
#include "../support/controlReveal.hpp"

#include <QSignalBlocker>
#include <cmath>

// Page size and units: the page metrics, the unit-driven inputs and the hover readout.

namespace stencil::gui {

  // The canonical page-format value ("A4"/"custom") behind the combo's display
  // label — the item DATA, never the label text (which carries the physical
  // size in the display unit and, while searching, whatever the user typed).
  QString MainWindow::pageSizeValue() const {
    return pageSize_->currentData().toString();
  }

  // Re-render the page-format combo labels in the active display unit. Items,
  // data, and the selection are untouched, so no change handlers fire.
  void MainWindow::applyUnitToPageCombo() {
    if (!pageSize_) return;
    fillPageSizeCombo(pageSize_, /*includeCustom=*/true, settings_.units);
  }

  // Page dimensions for the current selection, honoring custom W x H.
  core::PageSize MainWindow::currentPageDimensions() const {
    return core::pageDimensions(pageSizeValue().toStdString(),
                                canvas_->imageWidth(), canvas_->imageHeight(),
                                settings_.customPageWidth,
                                settings_.customPageHeight);
  }

  // Raw pixel -> page (cm), then the f(x)/f(y) formula transform, exactly as
  // the browser composes pixelToPageCoords (drawingApp.js ~1756).
  core::Point MainWindow::pageCoords(double imageX, double imageY) const {
    const auto dims = currentPageDimensions();
    const auto raw = core::pixelToPageRaw(imageX, imageY, dims,
                                          canvas_->imageWidth(),
                                          canvas_->imageHeight());
    core::Point p;
    p.x = core::FormulaParser::apply(settings_.formulaX.toStdString(), 'x', raw.x,
                                     settings_.allowFormulas);
    p.y = core::FormulaParser::apply(settings_.formulaY.toStdString(), 'y', raw.y,
                                     settings_.allowFormulas);
    return p;
  }

  // Active display unit (cm by default; inches scales cm by 1/2.54). Shared with
  // the hover tooltip via core::buildTooltipRows.
  core::UnitFormat MainWindow::unitFormat() const {
    if (settings_.units == "in") return {1.0 / 2.54, "in"};
    return {1.0, "cm"};
  }

  // Total real-world length of every drawn line segment, in centimetres. Uses the raw
  // per-axis px→cm scale of pixelToPageRaw (NOT the formula/pageCoords path), so it is
  // independent of the display unit and of any coordinate formulas — mirroring
  // browser/js/core/units.js layoutLineLengthCm. Cached on the project meta at save
  // time to feed the projects-list tooltip cheaply. 0 when nothing is measurable.
  double MainWindow::currentLineLengthCm() const {
    const core::PageSize dims = currentPageDimensions();  // cm; already landscape-swaps
    const int cw = canvas_->imageWidth(), ch = canvas_->imageHeight();
    if (cw <= 0 || ch <= 0) return 0.0;
    const double sx = dims.width / cw, sy = dims.height / ch;
    const auto sumLine = [&](const core::Line& ln) {
      double t = 0.0;
      const auto& pts = ln.points;
      for (std::size_t i = 1; i < pts.size(); ++i)
        t += std::hypot((pts[i].x - pts[i - 1].x) * sx, (pts[i].y - pts[i - 1].y) * sy);
      return t;
    };
    // Sum committed lines by const-ref (no allLines() copy), then the in-progress line if any.
    double total = 0.0;
    for (const auto& ln : canvas_->lines()) total += sumLine(ln);
    if (!canvas_->currentLine().points.empty()) total += sumLine(canvas_->currentLine());
    return total;
  }

  // Stamp the display-only tooltip fields onto `meta` from the live canvas: image px
  // dimensions (0 when there is no image) and the total drawn-line length in cm.
  void MainWindow::stampCanvasMeta(core::ProjectMeta& meta) const {
    const bool hasImg = canvas_->hasImage();
    meta.imageW = hasImg ? canvas_->imageWidth() : 0;
    meta.imageH = hasImg ? canvas_->imageHeight() : 0;
    meta.lineLengthCm = currentLineLengthCm();
  }

  // Render the custom page spinboxes + their suffix label in the active unit.
  // Model values stay in cm; signals are blocked so the programmatic setValue
  // here doesn't feed back through the valueChanged handlers.
  void MainWindow::applyUnitToPageInputs() {
    if (!customW_ || !customH_) return;
    const auto u = unitFormat();
    const bool inches = (settings_.units == "in");
    QSignalBlocker bw(customW_), bh(customH_);
    customW_->setDecimals(inches ? 2 : 1);
    customH_->setDecimals(inches ? 2 : 1);
    customW_->setValue(settings_.customPageWidth * u.factor);
    customH_->setValue(settings_.customPageHeight * u.factor);
  }

  // Reflect settings_.units in both unit controls without firing their handlers.
  void MainWindow::syncUnitControls() {
    const bool inches = settings_.units == "in";
    if (actUnitCm_ && actUnitIn_) {
      QSignalBlocker bc(actUnitCm_), bi(actUnitIn_);
      actUnitIn_->setChecked(inches);
      actUnitCm_->setChecked(!inches);
    }
    if (unitCombo_) {
      QSignalBlocker b(unitCombo_);
      unitCombo_->setCurrentIndex(inches ? 1 : 0);
    }
  }

  // Change the active display unit from any surface (menu or toolbar combo):
  // persist, keep both controls in sync, and refresh every length readout.
  void MainWindow::applyUnits(const QString& code) {
    const QString c = (code == "in") ? "in" : "cm";
    if (settings_.units == c) return;
    settings_.units = c;
    persistSettings();
    syncUnitControls();
    applyUnitToPageCombo();  // page-format labels re-render in the new unit
    applyUnitToPageInputs();
    onHovered(lastHoverX_, lastHoverY_);  // status bar + live tooltip
    onSelectionChanged();                 // selection panel rows
  }

  // Reuse the core page metrics exactly as the browser's pixelToPageCoords does,
  // so the page readout matches between the two front-ends. Status mirrors the
  // browser status bar: Pixel / Page / To edge, in brackets, in the active unit.
  void MainWindow::onHovered(double imageX, double imageY) {
    // No image, or a refresh replaying the cache while the cursor is off the canvas
    // (NaN — see lastHoverX_): nothing to measure, keep the bar empty.
    if (!canvas_->hasImage() || std::isnan(imageX) || std::isnan(imageY)) {
      updateStatusIdle();
      return;
    }
    lastHoverX_ = imageX;
    lastHoverY_ = imageY;
    const auto page = pageCoords(imageX, imageY);
    const auto dims = currentPageDimensions();
    const auto u = unitFormat();
    const QString lbl = QString::fromStdString(u.label);
    status_->setText(
        // Browser parity (drawingApp.js updateCoordStatus): dot separators.
        QString("Pixel (%1, %2)   \u00b7   Page (%3, %4) %5   \u00b7   To edge (%6, %7) %5")
            .arg(qRound(imageX))
            .arg(qRound(imageY))
            .arg(page.x * u.factor, 0, 'f', 2)
            .arg(page.y * u.factor, 0, 'f', 2)
            .arg(lbl)
            .arg((dims.width - page.x) * u.factor, 0, 'f', 2)
            .arg((dims.height - page.y) * u.factor, 0, 'f', 2));
  }

  // Show/hide the custom inputs and recompute when the page size changes.
  void MainWindow::onPageSizeChanged() {
    const bool custom = pageSizeValue() == "custom";
    revealControls(customGroup_, custom);
    settings_.pageSize = pageSizeValue();
    // Keep the canvas's default-crop aspect in sync with the selected page.
    {
      const core::PageSize page = naturalPageCm(pageSizeValue(),
                                                settings_.customPageWidth,
                                                settings_.customPageHeight);
      canvas_->setPageCm(page.width, page.height);
    }
    persistSettings();
    onHovered(lastHoverX_, lastHoverY_);
    onSelectionChanged();  // refresh panel cm for the new page size (GAP-2)
    remoteSync_->scheduleRemotePush();  // page format rides the layout — push to peers
  }

}  // namespace stencil::gui

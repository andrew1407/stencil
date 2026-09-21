#include "MainWindow.hpp"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include "MainWindow.hpp"
#include "mainWindowHelpers.hpp"
#include "ChatPlanTarget.hpp"
#include "LogoHoverFx.hpp"
#include "planExecutor.hpp"
#include "CanvasWidget.hpp"
#include "IncognitoOverlay.hpp"
#include "guiHelpers.hpp"
#include "ProjectsDialog.hpp"
#include "RemoteSyncController.hpp"
#include "ServerClient.hpp"
#include "../../support/control/reveal/controlReveal.hpp"

#include <QSignalBlocker>
#include <cmath>

// Page size and units: the page metrics, the unit-driven inputs and the hover readout.

namespace stencil::gui {

  // The item data, never the label text (which carries the physical size and, while searching,
  // whatever was typed).
  QString MainWindow::pageSizeValue() const {
    return units.pageSize->currentData().toString();
  }

  // Items, data and selection are untouched, so no change handlers fire.
  void MainWindow::applyUnitToPageCombo() {
    if (!units.pageSize) return;
    fillPageSizeCombo(units.pageSize, /*includeCustom=*/true, settings.units);
  }

  core::PageSize MainWindow::currentPageDimensions() const {
    return core::pageDimensions(pageSizeValue().toStdString(),
                                canvas->imageWidth(), canvas->imageHeight(),
                                settings.customPageWidth,
                                settings.customPageHeight);
  }

  // Raw pixel -> page (cm), then the formula transform, as the browser's pixelToPageCoords
  // (drawingApp.js ~1756).
  core::Point MainWindow::pageCoords(double imageX, double imageY) const {
    const auto dims = currentPageDimensions();
    const auto raw = core::pixelToPageRaw(imageX, imageY, dims,
                                          canvas->imageWidth(),
                                          canvas->imageHeight());
    core::Point p;
    p.x = core::FormulaParser::apply(settings.formulaX.toStdString(), 'x', raw.x,
                                     settings.allowFormulas);
    p.y = core::FormulaParser::apply(settings.formulaY.toStdString(), 'y', raw.y,
                                     settings.allowFormulas);
    return p;
  }

  // Inches scale cm by 1/2.54. Shared with the hover tooltip via core::buildTooltipRows.
  core::UnitFormat MainWindow::unitFormat() const {
    return {UnitsController::factor(settings.units), UnitsController::label(settings.units)};
  }

  // Raw px→cm per axis (not the formula path), independent of the display unit — mirrors
  // browser/js/core/settings/units.js layoutLineLengthCm. 0 when nothing is measurable.
  double MainWindow::currentLineLengthCm() const {
    const core::PageSize dims = currentPageDimensions();  // cm; already landscape-swaps
    const auto scale = UnitsController::pxToCm(dims.width, dims.height, canvas->imageWidth(),
                                               canvas->imageHeight());
    if (!scale.measurable()) return 0.0;
    const auto sumLine = [&](const core::Line& ln) {
      double t = 0.0;
      const auto& pts = ln.points;
      for (std::size_t i = 1; i < pts.size(); ++i)
        t += UnitsController::segmentCm(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y, scale);
      return t;
    };
    double total = 0.0;
    for (const auto& ln : canvas->getLines()) total += sumLine(ln);
    if (!canvas->getCurrentLine().points.empty()) total += sumLine(canvas->getCurrentLine());
    return total;
  }

  void MainWindow::stampCanvasMeta(core::ProjectMeta& meta) const {
    const bool hasImg = canvas->hasImage();
    meta.imageW = hasImg ? canvas->imageWidth() : 0;
    meta.imageH = hasImg ? canvas->imageHeight() : 0;
    meta.lineLengthCm = currentLineLengthCm();
  }

  // Model values stay in cm; signals blocked so the programmatic setValue does not feed back.
  void MainWindow::applyUnitToPageInputs() {
    if (!units.customW || !units.customH) return;
    const auto u = unitFormat();
    const int decimals = UnitsController::decimalsFor(settings.units);
    QSignalBlocker bw(units.customW), bh(units.customH);
    units.customW->setDecimals(decimals);
    units.customH->setDecimals(decimals);
    units.customW->setValue(settings.customPageWidth * u.factor);
    units.customH->setValue(settings.customPageHeight * u.factor);
  }

  void MainWindow::syncUnitControls() {
    const bool inches = UnitsController::isInches(settings.units);
    if (units.unitCm && units.unitIn) {
      QSignalBlocker bc(units.unitCm), bi(units.unitIn);
      units.unitIn->setChecked(inches);
      units.unitCm->setChecked(!inches);
    }
    if (units.unitCombo) {
      QSignalBlocker b(units.unitCombo);
      units.unitCombo->setCurrentIndex(inches ? 1 : 0);
    }
  }

  void MainWindow::applyUnits(const QString& code) {
    const QString c = UnitsController::canonicalUnit(code);
    if (settings.units == c) return;
    settings.units = c;
    persistSettings();
    syncUnitControls();
    applyUnitToPageCombo();  // page-format labels re-render in the new unit
    applyUnitToPageInputs();
    onHovered(lastHoverX, lastHoverY);  // status bar + live tooltip
    onSelectionChanged();                 // selection panel rows
  }

  // Status mirrors the browser status bar: Pixel / Page / To edge, in brackets, in the active
  // unit.
  void MainWindow::onHovered(double imageX, double imageY) {
    // No image, or a cache replay while the cursor is off the canvas (NaN — see lastHoverX): keep
    // the bar empty.
    if (!canvas->hasImage() || std::isnan(imageX) || std::isnan(imageY)) {
      updateStatusIdle();
      return;
    }
    lastHoverX = imageX;
    lastHoverY = imageY;
    const auto page = pageCoords(imageX, imageY);
    const auto dims = currentPageDimensions();
    const auto u = unitFormat();
    const QString lbl = QString::fromStdString(u.label);
    status->setText(
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

  void MainWindow::onPageSizeChanged() {
    const bool custom = pageSizeValue() == "custom";
    revealControls(units.customGroup, custom);
    settings.pageSize = pageSizeValue();
    {
      const core::PageSize page = naturalPageCm(pageSizeValue(),
                                                settings.customPageWidth,
                                                settings.customPageHeight);
      canvas->setPageCm(page.width, page.height);
    }
    persistSettings();
    onHovered(lastHoverX, lastHoverY);
    onSelectionChanged();  // refresh panel cm for the new page size (GAP-2)
    remoteSync->scheduleRemotePush();  // page format rides the layout — push to peers
  }

}  // namespace stencil::gui

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
#include "../support/controlReveal.hpp"

#include <QSignalBlocker>
#include <cmath>

// Page size and units: the page metrics, the unit-driven inputs and the hover readout.

namespace stencil::gui {

  // The item data, never the label text (which carries the physical size and, while searching,
  // whatever was typed).
  QString MainWindow::pageSizeValue() const {
    return units_.pageSize->currentData().toString();
  }

  // Items, data and selection are untouched, so no change handlers fire.
  void MainWindow::applyUnitToPageCombo() {
    if (!units_.pageSize) return;
    fillPageSizeCombo(units_.pageSize, /*includeCustom=*/true, settings_.units);
  }

  core::PageSize MainWindow::currentPageDimensions() const {
    return core::pageDimensions(pageSizeValue().toStdString(),
                                canvas_->imageWidth(), canvas_->imageHeight(),
                                settings_.customPageWidth,
                                settings_.customPageHeight);
  }

  // Raw pixel -> page (cm), then the formula transform, as the browser's pixelToPageCoords
  // (drawingApp.js ~1756).
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

  // Inches scale cm by 1/2.54. Shared with the hover tooltip via core::buildTooltipRows.
  core::UnitFormat MainWindow::unitFormat() const {
    return {UnitsController::factor(settings_.units), UnitsController::label(settings_.units)};
  }

  // Raw px→cm per axis (not the formula path), independent of the display unit — mirrors
  // browser/js/core/units.js layoutLineLengthCm. 0 when nothing is measurable.
  double MainWindow::currentLineLengthCm() const {
    const core::PageSize dims = currentPageDimensions();  // cm; already landscape-swaps
    const auto scale = UnitsController::pxToCm(dims.width, dims.height, canvas_->imageWidth(),
                                               canvas_->imageHeight());
    if (!scale.measurable()) return 0.0;
    const auto sumLine = [&](const core::Line& ln) {
      double t = 0.0;
      const auto& pts = ln.points;
      for (std::size_t i = 1; i < pts.size(); ++i)
        t += UnitsController::segmentCm(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y, scale);
      return t;
    };
    double total = 0.0;
    for (const auto& ln : canvas_->lines()) total += sumLine(ln);
    if (!canvas_->currentLine().points.empty()) total += sumLine(canvas_->currentLine());
    return total;
  }

  void MainWindow::stampCanvasMeta(core::ProjectMeta& meta) const {
    const bool hasImg = canvas_->hasImage();
    meta.imageW = hasImg ? canvas_->imageWidth() : 0;
    meta.imageH = hasImg ? canvas_->imageHeight() : 0;
    meta.lineLengthCm = currentLineLengthCm();
  }

  // Model values stay in cm; signals blocked so the programmatic setValue does not feed back.
  void MainWindow::applyUnitToPageInputs() {
    if (!units_.customW || !units_.customH) return;
    const auto u = unitFormat();
    const int decimals = UnitsController::decimalsFor(settings_.units);
    QSignalBlocker bw(units_.customW), bh(units_.customH);
    units_.customW->setDecimals(decimals);
    units_.customH->setDecimals(decimals);
    units_.customW->setValue(settings_.customPageWidth * u.factor);
    units_.customH->setValue(settings_.customPageHeight * u.factor);
  }

  void MainWindow::syncUnitControls() {
    const bool inches = UnitsController::isInches(settings_.units);
    if (units_.unitCm && units_.unitIn) {
      QSignalBlocker bc(units_.unitCm), bi(units_.unitIn);
      units_.unitIn->setChecked(inches);
      units_.unitCm->setChecked(!inches);
    }
    if (units_.unitCombo) {
      QSignalBlocker b(units_.unitCombo);
      units_.unitCombo->setCurrentIndex(inches ? 1 : 0);
    }
  }

  void MainWindow::applyUnits(const QString& code) {
    const QString c = UnitsController::canonicalUnit(code);
    if (settings_.units == c) return;
    settings_.units = c;
    persistSettings();
    syncUnitControls();
    applyUnitToPageCombo();  // page-format labels re-render in the new unit
    applyUnitToPageInputs();
    onHovered(lastHoverX_, lastHoverY_);  // status bar + live tooltip
    onSelectionChanged();                 // selection panel rows
  }

  // Status mirrors the browser status bar: Pixel / Page / To edge, in brackets, in the active
  // unit.
  void MainWindow::onHovered(double imageX, double imageY) {
    // No image, or a cache replay while the cursor is off the canvas (NaN — see lastHoverX_): keep
    // the bar empty.
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

  void MainWindow::onPageSizeChanged() {
    const bool custom = pageSizeValue() == "custom";
    revealControls(units_.customGroup, custom);
    settings_.pageSize = pageSizeValue();
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

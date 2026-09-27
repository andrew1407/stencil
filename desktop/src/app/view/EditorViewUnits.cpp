#include "MainWindow.hpp"
#include "EditorView.hpp"
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>
#include "mainWindowHelpers.hpp"
#include "CanvasWidget.hpp"
#include "guiHelpers.hpp"
#include "RemoteSyncController.hpp"
#include "../../support/control/reveal/controlReveal.hpp"

// Page size and units: the page metrics, the unit-driven inputs and the hover readout.

namespace stencil::gui {

  // Items, data and selection are untouched, so no change handlers fire.
  void EditorView::applyUnitToPageCombo() {
    if (!w.units.pageSize) return;
    fillPageSizeCombo(w.units.pageSize, /*includeCustom=*/true, w.settings.units);
  }

  // What the f(x,y) names resolve to: the page in cm, the image in pixels — absent until one
  // is open, so IMAGE_WIDTH / IMAGE_HEIGHT stay unknown — and the active display unit.
  core::FormulaContext EditorView::formulaContext() const {
    const auto dims = w.currentPageDimensions();
    core::FormulaContext ctx;
    ctx.pageWidthCm = dims.width;
    ctx.pageHeightCm = dims.height;
    if (w.canvas->hasImage()) {
      ctx.imageWidth = w.canvas->imageWidth();
      ctx.imageHeight = w.canvas->imageHeight();
    }
    ctx.unit = UnitsController::canonicalUnit(w.settings.units).toStdString();
    return ctx;
  }

  // Raw px→cm per axis (not the formula path), independent of the display unit — mirrors
  // browser/js/core/settings/units.js layoutLineLengthCm. 0 when nothing is measurable.
  double EditorView::currentLineLengthCm() const {
    const core::PageSize dims = w.currentPageDimensions();  // cm; already landscape-swaps
    const auto scale = UnitsController::pxToCm(dims.width, dims.height, w.canvas->imageWidth(),
                                               w.canvas->imageHeight());
    if (!scale.measurable()) return 0.0;
    const auto sumLine = [&](const core::Line& ln) {
      double t = 0.0;
      const auto& pts = ln.points;
      for (std::size_t i = 1; i < pts.size(); ++i)
        t += UnitsController::segmentCm(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y, scale);
      return t;
    };
    double total = 0.0;
    for (const auto& ln : w.canvas->getLines()) total += sumLine(ln);
    if (!w.canvas->getCurrentLine().points.empty()) total += sumLine(w.canvas->getCurrentLine());
    return total;
  }

  void EditorView::stampCanvasMeta(core::ProjectMeta& meta) const {
    const bool hasImg = w.canvas->hasImage();
    meta.imageW = hasImg ? w.canvas->imageWidth() : 0;
    meta.imageH = hasImg ? w.canvas->imageHeight() : 0;
    meta.lineLengthCm = currentLineLengthCm();
  }

  // Model values stay in cm; signals blocked so the programmatic setValue does not feed back.
  void EditorView::applyUnitToPageInputs() {
    if (!w.units.customW || !w.units.customH) return;
    const auto u = w.unitFormat();
    const int decimals = UnitsController::decimalsFor(w.settings.units);
    QSignalBlocker bw(w.units.customW), bh(w.units.customH);
    w.units.customW->setDecimals(decimals);
    w.units.customH->setDecimals(decimals);
    w.units.customW->setValue(w.settings.customPageWidth * u.factor);
    w.units.customH->setValue(w.settings.customPageHeight * u.factor);
  }

  void EditorView::syncUnitControls() {
    const bool inches = UnitsController::isInches(w.settings.units);
    if (w.units.unitCm && w.units.unitIn) {
      QSignalBlocker bc(w.units.unitCm), bi(w.units.unitIn);
      w.units.unitIn->setChecked(inches);
      w.units.unitCm->setChecked(!inches);
    }
    if (w.units.unitCombo) {
      QSignalBlocker b(w.units.unitCombo);
      w.units.unitCombo->setCurrentIndex(inches ? 1 : 0);
    }
  }

  void EditorView::applyUnits(const QString& code) {
    const QString c = UnitsController::canonicalUnit(code);
    if (w.settings.units == c) return;
    w.settings.units = c;
    w.persistSettings();
    syncUnitControls();
    applyUnitToPageCombo();  // page-format labels re-render in the new unit
    applyUnitToPageInputs();
    onHovered(lastHoverX, lastHoverY);  // status bar + live tooltip
    w.onSelectionChanged();                 // selection panel rows
  }

  // Status mirrors the browser status bar: Pixel / Page / To edge, in brackets, in the active
  // unit.
  void EditorView::onHovered(double imageX, double imageY) {
    // No image, or a cache replay while the cursor is off the canvas (NaN — see lastHoverX): keep
    // the bar empty.
    if (!w.canvas->hasImage() || std::isnan(imageX) || std::isnan(imageY)) {
      w.updateStatusIdle();
      return;
    }
    lastHoverX = imageX;
    lastHoverY = imageY;
    const auto page = w.pageCoords(imageX, imageY);
    const auto dims = w.currentPageDimensions();
    const auto u = w.unitFormat();
    const QString lbl = QString::fromStdString(u.label);
    w.status->setText(
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

  void EditorView::onPageSizeChanged() {
    const bool custom = w.pageSizeValue() == "custom";
    revealControls(w.units.customGroup, custom);
    w.settings.pageSize = w.pageSizeValue();
    {
      const core::PageSize page = naturalPageCm(w.pageSizeValue(),
                                                w.settings.customPageWidth,
                                                w.settings.customPageHeight);
      w.canvas->setPageCm(page.width, page.height);
    }
    w.persistSettings();
    onHovered(lastHoverX, lastHoverY);
    w.onSelectionChanged();  // refresh panel cm for the new page size (GAP-2)
    w.remoteSync->scheduleRemotePush();  // page format rides the layout — push to peers
  }

  // The browser's wireFormulaInputs commit: on settle, Enter, focus-out or a programmatic set —
  // never per keystroke.
  void EditorView::validateAndApplyFormulas() {
    // A focus-out commit also arrives during destruction, when the controllers are gone. Same
    // guard as the chat dock.
    if (w.tearingDown) return;
    if (w.tools.formulaCommitTimer) w.tools.formulaCommitTimer->stop();   // a direct call pre-empts the pause
    const QString fx = w.tools.formulaX->text().trimmed();
    const QString fy = w.tools.formulaY->text().trimmed();
    const core::FormulaContext ctx = formulaContext();
    const bool okX = core::FormulaParser::validate(fx.toStdString(), ctx);
    const bool okY = core::FormulaParser::validate(fy.toStdString(), ctx);
    w.tools.formulaError->setVisible(!okX || !okY);
    if (okX && okY) {
      w.settings.formulaX = fx;
      w.settings.formulaY = fy;
      w.persistSettings();
      onHovered(lastHoverX, lastHoverY);
      w.onSelectionChanged();  // refresh panel cm with the new formulas (GAP-2)
      w.remoteSync->scheduleRemotePush();  // formulas ride the layout — push to peers
    }
  }
}  // namespace stencil::gui

#include "MainWindow.hpp"
#include "CanvasWidget.hpp"
#include "zoomPan.hpp"

#include <QScrollBar>
#include <QSignalBlocker>
#include <QComboBox>
#include <QLabel>
#include <QScrollArea>
#include <algorithm>

// Zoom, fit and the cursor-anchored zoom; the page size and units the pointer readout converts
// with, and the readout's idle line.

namespace stencil::gui {

  void MainWindow::setZoom(double scale, bool syncCombo) {
    scale = core::clampScale(scale);  // shared [ZOOM_MIN, ZOOM_MAX] bound (core/state/zoomPan)
    canvas->setScale(scale);
    if (syncCombo) {
      const QString pct = QString::number(qRound(scale * 100)) + "%";
      // setEditText with NoInsert never appends list items; signals blocked so a programmatic zoom
      // does not re-trigger setZoom.
      QSignalBlocker block(zoom);
      zoom->setEditText(pct);
    }
    // The single debounced persistence path for every zoom route (browser zoom/pan.js persistZoom).
    parts.persistence.scheduleViewSave();
    parts.view.revealCanvasScrollbars();   // a zoom can grow/shrink the scrollable range — show it
  }

  void MainWindow::fitToWindow() {
    if (!canvas->hasImage()) return;
    const QSize vp = scroll->viewport()->size();
    const double sx = double(vp.width()) / canvas->imageWidth();
    const double sy = double(vp.height()) / canvas->imageHeight();
    setZoom(std::min(sx, sy) * 0.95);
  }

  // Keeps the image pixel under the cursor fixed; mirrors zoom/pan.js zoomToward via
  // core::anchoredZoom.
  void MainWindow::setZoomAnchored(double newScale,
                                   const QPoint& cursorInViewport) {
    if (!canvas->hasImage()) {
      setZoom(newScale);
      return;
    }
    const double oldScale = canvas->getScale();
    const double sl = scroll->horizontalScrollBar()->value();
    const double st = scroll->verticalScrollBar()->value();
    const auto z = core::anchoredZoom(sl, st, cursorInViewport.x(),
                                      cursorInViewport.y(), oldScale, newScale);
    setZoom(z.scale);
    parts.view.scrollTo(qRound(z.scrollLeft), qRound(z.scrollTop));
  }

  // The item data, never the label text (which carries the physical size and, while searching,
  // whatever was typed).
  QString MainWindow::pageSizeValue() const {
    return units.pageSize->currentData().toString();
  }

  core::PageSize MainWindow::currentPageDimensions() const {
    return core::pageDimensions(pageSizeValue().toStdString(),
                                canvas->imageWidth(), canvas->imageHeight(),
                                settings.customPageWidth,
                                settings.customPageHeight);
  }

  // Raw pixel -> page (cm), then the formula transform, as the browser's pixelToPageCoords
  // (pageMetrics.js). Both raw axes ride the context, so f(x) may read y and f(y) may read x.
  core::Point MainWindow::pageCoords(double imageX, double imageY) const {
    core::FormulaContext ctx = parts.view.formulaContext();
    const core::PageSize dims{ctx.pageWidthCm, ctx.pageHeightCm};
    const auto raw = core::pixelToPageRaw(imageX, imageY, dims,
                                          canvas->imageWidth(),
                                          canvas->imageHeight());
    ctx.x = raw.x;
    ctx.y = raw.y;
    core::Point p;
    p.x = core::FormulaParser::apply(settings.formulaX.toStdString(), 'x', raw.x,
                                     settings.allowFormulas, ctx);
    p.y = core::FormulaParser::apply(settings.formulaY.toStdString(), 'y', raw.y,
                                     settings.allowFormulas, ctx);
    return p;
  }

  // Inches scale cm by 1/2.54. Shared with the hover tooltip via core::buildTooltipRows.
  core::UnitFormat MainWindow::unitFormat() const {
    return {UnitsController::factor(settings.units), UnitsController::label(settings.units)};
  }

  void MainWindow::updateStatusIdle() {
    // Empty with the pointer off the canvas or no image (browser parity).
    status->setText(QString());
  }

}  // namespace stencil::gui

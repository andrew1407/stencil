#include "mainWindow.hpp"
#include "mainWindowShared.hpp"
#include "mainWindow.hpp"
#include "stayOpenMenu.hpp"
#include "mainWindowHelpers.hpp"
#include "chatPlanTarget.hpp"
#include "logoHoverFx.hpp"
#include "chatMenuPanel.hpp"
#include "planExecutor.hpp"
#include "openImageDialog.hpp"
#include "canvasWidget.hpp"
#include "overlayScrollArea.hpp"
#include "dropZonesOverlay.hpp"
#include "incognitoOverlay.hpp"
#include "zoomPan.hpp"
#include "menuHotkeys.hpp"
#include "menuShimmer.hpp"
#include "searchCombo.hpp"
#include "controlsPill.hpp"
#include "linksDialog.hpp"
#include "descriptionDialog.hpp"
#include "keywordsDialog.hpp"
#include "notifications.hpp"
#include "projectsDialog.hpp"
#include "connectDialog.hpp"
#include "selectionPanel.hpp"
#include "shortcutsDialog.hpp"
#include "../support/appTooltip.hpp"
#include "../support/disintegrateOverlay.hpp"
#include "../support/controlSwap.hpp"
#include "../support/wrapRow.hpp"
#include "../support/modalChrome.hpp"
#include "../support/iconMotion.hpp"
#include "../support/hoverSlide.hpp"
#include "../support/shimmerOverlay.hpp"

#include <QLayout>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QToolBar>
#include <algorithm>

// Zoom, fit, precise scroll and the toolbar extent slide.

namespace stencil::gui {

  void MainWindow::zoomIn() { setZoom(canvas_->scale() * 1.25); }
  void MainWindow::zoomOut() { setZoom(canvas_->scale() * 0.8); }

  void MainWindow::setZoom(double scale, bool syncCombo) {
    scale = core::clampScale(scale);  // shared [ZOOM_MIN, ZOOM_MAX] bound (core/state/zoomPan)
    canvas_->setScale(scale);
    if (syncCombo) {
      const QString pct = QString::number(qRound(scale * 100)) + "%";
      // setEditText with NoInsert never appends list items; signals blocked so a programmatic zoom
      // does not re-trigger setZoom.
      QSignalBlocker block(zoom_);
      zoom_->setEditText(pct);
    }
    // The single debounced persistence path for every zoom route (browser zoomPan.js persistZoom).
    scheduleViewSave();
    revealCanvasScrollbars();   // a zoom can grow/shrink the scrollable range — show it
  }

  // Invisible until an actual pan/zoom, never from hovering.
  void MainWindow::revealCanvasScrollbars() {
    // A zoom resizes canvas_ without resizing scroll_, so relayout() directly; static_cast because
    // the subclass has no Q_OBJECT (overlayScrollArea.hpp).
    if (scroll_) static_cast<OverlayScrollArea*>(scroll_)->relayout();
    // A pan tick fires this twice per frame (both scrollbars); skip until the timer has burnt some
    // fuse.
    const bool shown = vScrollOpacity_ && vScrollOpacity_->opacity() >= 1.0
                       && hScrollOpacity_ && hScrollOpacity_->opacity() >= 1.0;
    if (shown && scrollbarHideTimer_ && scrollbarHideTimer_->isActive()
        && scrollbarHideTimer_->remainingTime() > 800)
      return;
    if (vScrollOpacity_) vScrollOpacity_->setOpacity(1.0);
    if (hScrollOpacity_) hScrollOpacity_->setOpacity(1.0);
    canvasScrollBar(Qt::Vertical)->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    canvasScrollBar(Qt::Horizontal)->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    scheduleScrollbarHide();
  }

  QScrollBar* MainWindow::canvasScrollBar(Qt::Orientation o) const {
    return static_cast<OverlayScrollArea*>(scroll_)->overlayBar(o);
  }

  // Unless the pointer sits on a bar; eventFilter's Leave branch calls this once it lifts.
  void MainWindow::scheduleScrollbarHide() {
    if (!scrollbarHideTimer_) return;
    if (scrollbarHovered_) { scrollbarHideTimer_->stop(); return; }
    scrollbarHideTimer_->start(900);
  }

  void MainWindow::fitToWindow() {
    if (!canvas_->hasImage()) return;
    const QSize vp = scroll_->viewport()->size();
    const double sx = double(vp.width()) / canvas_->imageWidth();
    const double sy = double(vp.height()) / canvas_->imageHeight();
    setZoom(std::min(sx, sy) * 0.95);
  }

  void MainWindow::setToolbarsVisible(bool on) {
    for (QToolBar* tb : findChildren<QToolBar*>()) { tb->setMaximumHeight(QWIDGETSIZE_MAX); tb->setVisible(on); }
    positionOverlayArrows();
  }

  void MainWindow::setToolbarsShown(bool show, bool animate) {
    toolbarsShown_ = show;
    refreshStatusHintVisibility();
    // The header row always stays, as the browser's header keeps the pill/title.
    QList<QToolBar*> bars;
    for (QToolBar* b : findChildren<QToolBar*>())
      if (b != headerToolbar_) bars.append(b);
    if (bars.isEmpty()) return;
    spinControlsPill(animate);   // the pill's chevron turns with the rows
    if (!animate) {
      if (barsAnim_) { barsAnim_->stop(); barsAnim_->deleteLater(); barsAnim_ = nullptr; }
      for (QToolBar* b : bars) { b->setMinimumHeight(0); b->setMaximumHeight(QWIDGETSIZE_MAX); b->setVisible(show); }
      positionOverlayArrows();
      return;
    }
    animateBarsHeight(bars, show);
  }

  // Pure geometry (setFixedHeight pins min==max each frame so QMainWindow's layout cannot override
  // it); no opacity effect, so no flicker.
  void MainWindow::animateBarsHeight(const QList<QToolBar*>& bars, bool show) {
    if (bars.isEmpty()) return;
    if (barsAnim_) { barsAnim_->stop(); barsAnim_->deleteLater(); barsAnim_ = nullptr; }
    auto release = [bars] {
      for (QToolBar* b : bars) { b->setMinimumHeight(0); b->setMaximumHeight(QWIDGETSIZE_MAX); }
    };
    // A hidden bar's sizeHint carries the height pinned at the old width, so the show path lets
    // the layout run before measuring.
    if (show) {
      release();
      for (QToolBar* b : bars) b->show();
      if (QLayout* l = layout()) l->activate();
      for (QToolBar* b : bars)
        if (WrapRow* row = wrapRowIn(b)) row->remeasure();
      if (QLayout* l = layout()) l->activate();
    }
    int full = 0;
    for (QToolBar* b : bars) full = std::max(full, b->sizeHint().height());
    if (full <= 0) full = 40;
    const int from = show ? 0 : (bars.first()->height() > 0 ? bars.first()->height() : full);
    const int to = show ? full : 0;
    // A show has to photograph the rows at full height before flattening them, so the pin below
    // runs after the flight.
    QPointer<gui::DisintegrateOverlay> dustFx;
    if (show) {
      for (QToolBar* b : bars) { b->setFixedHeight(full); b->show(); }
      if (QLayout* l = layout()) l->activate();
      dustFx = barsSurfaceFlight(bars, /*gather=*/true, FOLD_DUST_IN_MS);
      for (QToolBar* b : bars) b->setFixedHeight(0);
    }
    else dustFx = barsSurfaceFlight(bars, /*gather=*/false, FOLD_DUST_OUT_MS);
    barsAnim_ = startExtentSlide(
        this, from, to, show ? FOLD_MS : FOLD_OUT_MS,
        pinAndRaiseDust(
            [bars, this](int v) {
              for (QToolBar* b : bars) b->setFixedHeight(v);  // pin min==max on every row
              positionOverlayArrows();
            },
            dustFx),
        [this, bars, show, release] {
          release();
          if (!show) for (QToolBar* b : bars) b->hide();
          barsAnim_ = nullptr;
          positionOverlayArrows();
        });
  }

  void MainWindow::scrollTo(int x, int y) {
    auto* hb = scroll_->horizontalScrollBar();
    auto* vb = scroll_->verticalScrollBar();
    hb->setValue(std::clamp(x, hb->minimum(), hb->maximum()));
    vb->setValue(std::clamp(y, vb->minimum(), vb->maximum()));
  }

  // Keeps the image pixel under the cursor fixed; mirrors zoomPan.js zoomToward via
  // core::anchoredZoom.
  void MainWindow::setZoomAnchored(double newScale,
                                   const QPoint& cursorInViewport) {
    if (!canvas_->hasImage()) {
      setZoom(newScale);
      return;
    }
    const double oldScale = canvas_->scale();
    const double sl = scroll_->horizontalScrollBar()->value();
    const double st = scroll_->verticalScrollBar()->value();
    const auto z = core::anchoredZoom(sl, st, cursorInViewport.x(),
                                      cursorInViewport.y(), oldScale, newScale);
    setZoom(z.scale);
    scrollTo(qRound(z.scrollLeft), qRound(z.scrollTop));
  }

}  // namespace stencil::gui

#include "MainWindow.hpp"
#include "mainWindowShared.hpp"
#include "MainWindow.hpp"
#include "StayOpenMenu.hpp"
#include "mainWindowHelpers.hpp"
#include "ChatPlanTarget.hpp"
#include "LogoHoverFx.hpp"
#include "ChatMenuPanel.hpp"
#include "planExecutor.hpp"
#include "OpenImageDialog.hpp"
#include "CanvasWidget.hpp"
#include "OverlayScrollArea.hpp"
#include "DropZonesOverlay.hpp"
#include "IncognitoOverlay.hpp"
#include "zoomPan.hpp"
#include "MenuHotkeys.hpp"
#include "MenuShimmer.hpp"
#include "SearchCombo.hpp"
#include "ControlsPill.hpp"
#include "LinksDialog.hpp"
#include "DescriptionDialog.hpp"
#include "KeywordsDialog.hpp"
#include "Notifications.hpp"
#include "ProjectsDialog.hpp"
#include "ConnectDialog.hpp"
#include "SelectionPanel.hpp"
#include "ShortcutsDialog.hpp"
#include "../support/AppTooltip.hpp"
#include "../support/DisintegrateOverlay.hpp"
#include "../support/controlSwap.hpp"
#include "../support/WrapRow.hpp"
#include "../support/modalChrome.hpp"
#include "../support/iconMotion.hpp"
#include "../support/HoverSlide.hpp"
#include "../support/ShimmerOverlay.hpp"

#include <QLayout>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QToolBar>
#include <algorithm>

// Zoom, fit, precise scroll and the toolbar extent slide.

namespace stencil::gui {

  void MainWindow::zoomIn() { setZoom(canvas->getScale() * 1.25); }
  void MainWindow::zoomOut() { setZoom(canvas->getScale() * 0.8); }

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
    // The single debounced persistence path for every zoom route (browser zoomPan.js persistZoom).
    scheduleViewSave();
    revealCanvasScrollbars();   // a zoom can grow/shrink the scrollable range — show it
  }

  // Invisible until an actual pan/zoom, never from hovering.
  void MainWindow::revealCanvasScrollbars() {
    // A zoom resizes canvas without resizing scroll, so relayout() directly; static_cast because
    // the subclass has no Q_OBJECT (OverlayScrollArea.hpp).
    if (scroll) static_cast<OverlayScrollArea*>(scroll)->relayout();
    // A pan tick fires this twice per frame (both scrollbars); skip until the timer has burnt some
    // fuse.
    const bool shown = vScrollOpacity && vScrollOpacity->opacity() >= 1.0
                       && hScrollOpacity && hScrollOpacity->opacity() >= 1.0;
    if (shown && scrollbarHideTimer && scrollbarHideTimer->isActive()
        && scrollbarHideTimer->remainingTime() > 800)
      return;
    if (vScrollOpacity) vScrollOpacity->setOpacity(1.0);
    if (hScrollOpacity) hScrollOpacity->setOpacity(1.0);
    canvasScrollBar(Qt::Vertical)->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    canvasScrollBar(Qt::Horizontal)->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    scheduleScrollbarHide();
  }

  QScrollBar* MainWindow::canvasScrollBar(Qt::Orientation o) const {
    return static_cast<OverlayScrollArea*>(scroll)->overlayBar(o);
  }

  // Unless the pointer sits on a bar; eventFilter's Leave branch calls this once it lifts.
  void MainWindow::scheduleScrollbarHide() {
    if (!scrollbarHideTimer) return;
    if (scrollbarHovered) { scrollbarHideTimer->stop(); return; }
    scrollbarHideTimer->start(900);
  }

  void MainWindow::fitToWindow() {
    if (!canvas->hasImage()) return;
    const QSize vp = scroll->viewport()->size();
    const double sx = double(vp.width()) / canvas->imageWidth();
    const double sy = double(vp.height()) / canvas->imageHeight();
    setZoom(std::min(sx, sy) * 0.95);
  }

  void MainWindow::setToolbarsVisible(bool on) {
    for (QToolBar* tb : findChildren<QToolBar*>()) { tb->setMaximumHeight(QWIDGETSIZE_MAX); tb->setVisible(on); }
    positionOverlayArrows();
  }

  void MainWindow::setToolbarsShown(bool show, bool animate) {
    toolbarsShown = show;
    refreshStatusHintVisibility();
    // The header row always stays, as the browser's header keeps the pill/title.
    QList<QToolBar*> bars;
    for (QToolBar* b : findChildren<QToolBar*>())
      if (b != headerToolbar) bars.append(b);
    if (bars.isEmpty()) return;
    spinControlsPill(animate);   // the pill's chevron turns with the rows
    if (!animate) {
      if (barsAnim) { barsAnim->stop(); barsAnim->deleteLater(); barsAnim = nullptr; }
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
    if (barsAnim) { barsAnim->stop(); barsAnim->deleteLater(); barsAnim = nullptr; }
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
    barsAnim = startExtentSlide(
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
          barsAnim = nullptr;
          positionOverlayArrows();
        });
  }

  void MainWindow::scrollTo(int x, int y) {
    auto* hb = scroll->horizontalScrollBar();
    auto* vb = scroll->verticalScrollBar();
    hb->setValue(std::clamp(x, hb->minimum(), hb->maximum()));
    vb->setValue(std::clamp(y, vb->minimum(), vb->maximum()));
  }

  // Keeps the image pixel under the cursor fixed; mirrors zoomPan.js zoomToward via
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
    scrollTo(qRound(z.scrollLeft), qRound(z.scrollTop));
  }

}  // namespace stencil::gui

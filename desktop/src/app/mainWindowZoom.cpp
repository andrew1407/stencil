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
    scale = core::clampScale(scale);  // shared [kZoomMin, kZoomMax] bound (core/state/zoomPan)
    canvas_->setScale(scale);
    if (syncCombo) {
      const QString pct = QString::number(qRound(scale * 100)) + "%";
      // setEditText (with NoInsert) only updates the visible text — it never
      // appends list items, so Ctrl+wheel no longer accumulates entries. Block
      // signals so reflecting a programmatic zoom doesn't re-trigger setZoom.
      QSignalBlocker block(zoom_);
      zoom_->setEditText(pct);
    }
    // The single debounced persistence path for every zoom route (wheel/hold steps,
    // fitToWindow, the zoom combo, the fullscreen zoom's final landing) — browser parity,
    // zoomPan.js's own persistZoom called from this same central setZoom.
    scheduleViewSave();
    revealCanvasScrollbars();   // a zoom can grow/shrink the scrollable range — show it
  }

  // Fade both canvas scrollbars in — invisible until an actual pan/zoom, never just from
  // hovering the canvas — and (re)start the idle timer that fades them back out.
  void MainWindow::revealCanvasScrollbars() {
    // A zoom resizes canvas_ without necessarily resizing scroll_ itself, so the overlay's
    // own event-based recompute never sees it — call relayout() directly instead. scroll_ is
    // always this concrete type (see the ctor); static_cast, not qobject_cast, since the
    // subclass carries no Q_OBJECT/moc pass (see overlayScrollArea.hpp).
    if (scroll_) static_cast<OverlayScrollArea*>(scroll_)->relayout();
    // Already revealed with the hide timer freshly armed: a pan tick fires this twice
    // (both scrollbars) per frame — skip the opacity writes + timer restart until the
    // timer has actually burnt some of its fuse.
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

  // (Re)arms the fade-out timer, unless the pointer is sitting on a bar right now — the
  // Leave-event branch in eventFilter is what actually calls this once the pointer lifts.
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
    // Reset any leftover animated max-height (a mid-animation state) before showing/hiding.
    for (QToolBar* tb : findChildren<QToolBar*>()) { tb->setMaximumHeight(QWIDGETSIZE_MAX); tb->setVisible(on); }
    positionOverlayArrows();
  }

  void MainWindow::setToolbarsShown(bool show, bool animate) {
    toolbarsShown_ = show;
    refreshStatusHintVisibility();
    // The header row (Controls pill + project name) always stays — collapse only the tool rows,
    // mirroring the browser where the header keeps the pill/title while the body hides.
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

  // Height slide shared by the pill collapse/expand and the fullscreen edge-hover reveal. Pure geometry
  // (setFixedHeight pins min==max each frame so QMainWindow's layout can't override it) — no opacity /
  // graphics effect, which is what keeps it flicker-free through QMainWindow's per-frame relayout.
  void MainWindow::animateBarsHeight(const QList<QToolBar*>& bars, bool show) {
    if (bars.isEmpty()) return;
    if (barsAnim_) { barsAnim_->stop(); barsAnim_->deleteLater(); barsAnim_ = nullptr; }
    auto release = [bars] {
      for (QToolBar* b : bars) { b->setMinimumHeight(0); b->setMaximumHeight(QWIDGETSIZE_MAX); }
    };
    // Natural height each bar expands to. The rows wrap, so it follows the width they are
    // about to have — and a hidden bar's sizeHint carries the height pinned at the OLD one,
    // so the show path lets the layout hand them the real width before measuring.
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
    // Dust (barsSurfaceFlight): the rows come apart into motes streaming past the top
    // edge and gather back out of it. A show has to photograph them at their full height
    // BEFORE flattening them to zero, so the pin below runs after the flight.
    QPointer<gui::DisintegrateOverlay> dustFx;
    if (show) {
      for (QToolBar* b : bars) { b->setFixedHeight(full); b->show(); }
      if (QLayout* l = layout()) l->activate();
      dustFx = barsSurfaceFlight(bars, /*gather=*/true, kFoldDustInMs);
      for (QToolBar* b : bars) b->setFixedHeight(0);
    }
    else dustFx = barsSurfaceFlight(bars, /*gather=*/false, kFoldDustOutMs);
    barsAnim_ = startExtentSlide(
        this, from, to, show ? kFoldMs : kFoldOutMs,
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

  // precise scroll + anchored zoom (core/zoomPan math)
  void MainWindow::scrollTo(int x, int y) {
    auto* hb = scroll_->horizontalScrollBar();
    auto* vb = scroll_->verticalScrollBar();
    hb->setValue(std::clamp(x, hb->minimum(), hb->maximum()));
    vb->setValue(std::clamp(y, vb->minimum(), vb->maximum()));
  }

  // Zoom toward a cursor position (viewport coords), keeping the image pixel under
  // the cursor fixed. Mirrors zoomPan.js zoomToward via core::anchoredZoom.
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

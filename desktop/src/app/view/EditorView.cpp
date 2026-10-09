#include "MainWindow.hpp"
#include "EditorView.hpp"
#include "mainWindowShared.hpp"
#include "PanelSlide.hpp"
#include "mainWindowHelpers.hpp"
#include "CanvasWidget.hpp"
#include "Notifications.hpp"
#include "OverlayScrollArea.hpp"
#include "../../support/control/WrapRow.hpp"
#include "controlReveal.hpp"

#include <QLabel>
#include <QToolBar>

// Zoom steps, precise scroll and the overlay scrollbars; the toolbar extent slide, the dust flights
// that ride it, and the status hint's visibility.

namespace stencil::gui {

  void EditorView::zoomIn() { w.setZoom(w.canvas->getScale() * 1.25); }

  void EditorView::zoomOut() { w.setZoom(w.canvas->getScale() * 0.8); }

  // Invisible until an actual pan/zoom, never from hovering.
  void EditorView::revealCanvasScrollbars() {
    // A zoom resizes canvas without resizing scroll, so relayout() directly; static_cast because
    // the subclass has no Q_OBJECT (OverlayScrollArea.hpp).
    if (w.scroll) static_cast<OverlayScrollArea*>(w.scroll)->relayout();
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

  QScrollBar* EditorView::canvasScrollBar(Qt::Orientation o) const {
    return static_cast<OverlayScrollArea*>(w.scroll)->overlayBar(o);
  }

  // Unless the pointer sits on a bar; eventFilter's Leave branch calls this once it lifts.
  void EditorView::scheduleScrollbarHide() {
    if (!scrollbarHideTimer) return;
    if (scrollbarHovered) { scrollbarHideTimer->stop(); return; }
    scrollbarHideTimer->start(900);
  }

  void EditorView::setToolbarsVisible(bool on) {
    releaseBarsVeil(w.findChildren<QToolBar*>());
    for (QToolBar* tb : w.findChildren<QToolBar*>()) { tb->setMaximumHeight(QWIDGETSIZE_MAX); tb->setVisible(on); }
    w.positionOverlayArrows();
  }

  void EditorView::setToolbarsShown(bool show, bool animate) {
    w.toolbarsShown = show;
    refreshStatusHintVisibility();
    // The header row always stays, as the browser's header keeps the pill/title.
    QList<QToolBar*> bars;
    for (QToolBar* b : w.findChildren<QToolBar*>())
      if (b != w.tools.headerToolbar) bars.append(b);
    if (bars.isEmpty()) return;
    w.spinControlsPill(animate);   // the pill's chevron turns with the rows
    if (!animate) {
      if (barsAnim) { barsAnim->stop(); barsAnim->deleteLater(); barsAnim = nullptr; }
      releaseBarsVeil(bars);   // a flight still in the air must not leave the rows invisible
      for (QToolBar* b : bars) { b->setMinimumHeight(0); b->setMaximumHeight(QWIDGETSIZE_MAX); b->setVisible(show); }
      w.positionOverlayArrows();
      return;
    }
    animateBarsHeight(bars, show);
  }

  // setFixedHeight pins min==max each frame so QMainWindow's layout cannot override it; the only
  // opacity is the flight's constant veil, never a per-frame fade, so nothing flickers.
  void EditorView::animateBarsHeight(const QList<QToolBar*>& bars, bool show) {
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
      if (QLayout* l = w.editor->layout()) l->activate();
      for (QToolBar* b : bars)
        if (WrapRow* row = wrapRowIn(b)) row->remeasure();
      if (QLayout* l = w.editor->layout()) l->activate();
    }
    int full = 0;
    for (QToolBar* b : bars) full = std::max(full, b->sizeHint().height());
    if (full <= 0) full = 40;
    const int from = show ? 0 : (bars.first()->height() > 0 ? bars.first()->height() : full);
    const int to = show ? full : 0;
    // A show has to photograph the rows at full height before flattening them, so the pin below
    // runs after the flight.
    QPointer<gui::DisintegrateOverlay> dustFx;
    const PanelFoldClocks& fold = panelFoldClocks();
    if (show) {
      for (QToolBar* b : bars) { b->setFixedHeight(full); b->show(); }
      if (QLayout* l = w.editor->layout()) l->activate();
      dustFx = barsSurfaceFlight(bars, /*gather=*/true, fold.dustInMs);
      for (QToolBar* b : bars) b->setFixedHeight(0);
    }
    else dustFx = barsSurfaceFlight(bars, /*gather=*/false, fold.dustOutMs);
    barsAnim = startExtentSlide(
        &w, from, to, show ? fold.slideInMs : fold.slideOutMs,
        pinAndRaiseDust(
            [bars, this](int v) {
              for (QToolBar* b : bars) b->setFixedHeight(v);  // pin min==max on every row
              w.positionOverlayArrows();
            },
            dustFx),
        [this, bars, show, release] {
          release();
          if (!show) for (QToolBar* b : bars) b->hide();
          barsAnim = nullptr;
          w.positionOverlayArrows();
        });
  }

  void EditorView::scrollTo(int x, int y) {
    auto* hb = w.scroll->horizontalScrollBar();
    auto* vb = w.scroll->verticalScrollBar();
    hb->setValue(std::clamp(x, hb->minimum(), hb->maximum()));
    vb->setValue(std::clamp(y, vb->minimum(), vb->maximum()));
  }

  namespace {
    constexpr const char* BARS_VEIL_NAME = "barsVeil";   // tells the rows' flight veil from other effects
  }  // namespace

  // The tool rows collapse as ONE block: their union rect is the picture and the motes stream past the top edge (browser dockAwayPoint(box, 'top')).
  QPointer<gui::DisintegrateOverlay> EditorView::barsSurfaceFlight(
      const QList<QToolBar*>& bars, bool gather, int ms) {
    if (!support::isDustMotionOk()) return nullptr;
    QRect picture;
    for (QToolBar* b : bars) {
      if (!b->isVisible() || b->width() < 8 || b->height() < 8) continue;
      picture |= QRect(b->mapTo(w.editor, QPoint(0, 0)), b->size());
    }
    if (picture.width() < 8 || picture.height() < 8) return nullptr;
    const qreal dpr = w.devicePixelRatioF();
    QPixmap snap(picture.size() * dpr);
    snap.setDevicePixelRatio(dpr);
    snap.fill(Qt::transparent);
    for (QToolBar* b : bars) {
      if (!b->isVisible() || b->width() < 8 || b->height() < 8) continue;
      b->render(&snap, b->mapTo(w.editor, QPoint(0, 0)) - picture.topLeft(), QRegion(),
                QWidget::DrawWindowBackground | QWidget::DrawChildren);
    }
    QPointer<gui::DisintegrateOverlay> fx = gui::DisintegrateOverlay::overSurface(
        snap, picture, w.editor, dockAwayPoint(picture, Qt::TopDockWidgetArea), gather, ms,
        w.palette().color(QPalette::WindowText));
    if (!fx) return fx;
    // Held for the whole flight and handed over at its end, as the chat's is: a row revealed
    // while it still slides shows its controls twice, one copy off the other.
    QList<QPair<QPointer<QToolBar>, QPointer<QGraphicsEffect>>> veils;
    for (QToolBar* b : bars) {
      QGraphicsOpacityEffect* veil = gui::veilBehindDust(b);
      veil->setObjectName(QLatin1String(BARS_VEIL_NAME));
      veils.append({b, veil});
    }
    // Only its own veils: a fold that interrupted this one has veiled the rows afresh.
    QObject::connect(fx, &QObject::destroyed, &w, [veils] {
      for (const auto& [b, veil] : veils)
        if (b && veil && b->graphicsEffect() == veil) b->setGraphicsEffect(nullptr);
    });
    return fx;
  }

  void EditorView::releaseBarsVeil(const QList<QToolBar*>& bars) {
    for (QToolBar* b : bars) {
      QGraphicsEffect* fx = b->graphicsEffect();
      if (fx && fx->objectName() == QLatin1String(BARS_VEIL_NAME)) b->setGraphicsEffect(nullptr);
    }
  }

  // The "?" is the COLLAPSED state's readout: shown only with the rows hidden AND an image open or incognito on.
  void EditorView::refreshStatusHintVisibility() {
    // The size line goes with the tool rows; the "?" takes over. Two readouts, one at a time.
    if (w.tools.imageSizeInfo) w.tools.imageSizeInfo->setVisible(w.toolbarsShown);
    // The badge rides that line, and comes and goes in the selected motion: dust in the
    // particle modes, the slot alone in `slide`, a plain show/hide under `none`.
    if (w.tools.incognitoTag) {
      w.reserveImageInfoHeight();
      revealControls(w.tools.incognitoTag, w.toolbarsShown && w.incognito);
    }
    if (!w.tools.statusHintAction) return;
    const bool live = (w.canvas && w.canvas->hasImage()) || w.incognito;
    w.tools.statusHintAction->setVisible(live && !w.toolbarsShown);
  }

  // Drawing with lines or points hidden puts down something that does not show: both come back,
  // through their own toggles (browser core/draw/mode.js startDrawingMode).
  void EditorView::showLinesForDrawing() {
    if (w.acts.showLines->isChecked() && w.acts.showPoints->isChecked()) return;
    w.acts.showLines->setChecked(true);
    w.acts.showPoints->setChecked(true);
    if (w.notify) w.notify->info(QStringLiteral("Lines and points shown again to draw"));
  }

}  // namespace stencil::gui

#include "MainWindow.hpp"
#include "EditorView.hpp"
#include "mainWindowHelpers.hpp"
#include "iconSet.hpp"
#include "SelectionPanel.hpp"
#include "../../support/control/reveal/controlReveal.hpp"
#include "CanvasWidget.hpp"
#include "zoomPan.hpp"
#include "../../support/uiTimings.hpp"
#include "theme.hpp"
#include "iconDrag.hpp"

#include <QScrollArea>
#include <QLabel>
#include <QMenuBar>
#include <QPointer>
#include <QToolBar>

// Fullscreen: the toggle, the edge-hover reveal of the bars it hid, driven by the pointer's moves,
// and the zoom hand-off across the switch.

namespace stencil::gui {

  void EditorView::toggleFullscreen() {
    // Whatever is in the air goes first: the switch hides every toolbar, and a cloud would be left over the bare canvas.
    stopDustClouds(&w);
    if (w.fs.active) {
      // Exit
      w.fs.active = false;
      syncFullscreenGlyph();
      markFullscreenBars(false);
      w.fs.bars.clear();
      // A leftover slide / fixed height would fight the restore below.
      if (barsAnim) { barsAnim->stop(); barsAnim->deleteLater(); barsAnim = nullptr; }
      for (QToolBar* b : w.findChildren<QToolBar*>()) { b->setMinimumHeight(0); b->setMaximumHeight(QWIDGETSIZE_MAX); }
      w.fs.barsShown = false;
      w.fs.panelShown = false;
      beginFullscreenZoom();
      w.showNormal();
      if (w.menuBar()) w.menuBar()->setVisible(true);
      if (w.status) w.status->setVisible(true);     // restore the coord readout
      if (w.tools.dropHint) w.tools.dropHint->setVisible(true);  // …and the drag & drop hint (browser: .drop-hint)
      // The size readout's DOCK too: its height is pinned, so hiding only the bar left an empty band.
      if (w.tools.imageInfoHost) w.tools.imageInfoHost->setVisible(true);
      if (w.tools.imageInfoDock) w.tools.imageInfoDock->setVisible(true);
      // The header row ALWAYS returns — it is the only way to re-show the tool rows.
      if (w.tools.headerToolbar) { w.tools.headerToolbar->setMaximumHeight(QWIDGETSIZE_MAX); w.tools.headerToolbar->setVisible(true); }
      setToolbarsShown(w.fs.wasToolbars, false);
      // Sync the checks WITHOUT re-firing the animated toggled handlers.
      if (w.acts.toolbars) { QSignalBlocker b(w.acts.toolbars); w.acts.toolbars->setChecked(w.fs.wasToolbars); }
      if (w.acts.panel) { QSignalBlocker b(w.acts.panel); w.acts.panel->setChecked(w.fs.wasPanel); }
      w.setPanelShown(w.fs.wasPanel, false);
      w.positionOverlayArrows();
    } else {
      // Enter: hide the top menu and the points panel; a cursor poll re-reveals each from its edge (browser parity).
      w.fs.wasToolbars = w.acts.toolbars ? w.acts.toolbars->isChecked() : true;
      w.fs.wasPanel = w.acts.panel ? w.acts.panel->isChecked() : true;   // was the panel expanded (vs rail)?
      // Capture the real width BEFORE hiding, or the edge reveal slides to setPanelShown's 320px default and snaps back.
      if (!w.panelSlide.anim && w.selPanel->isVisible() && w.selPanel->width() > 120) w.panelSlide.restoreWidth = w.selPanel->width();
      setToolbarsVisible(false);
      if (w.menuBar()) w.menuBar()->setVisible(false);
      if (w.status) w.status->setVisible(false);     // hide the coord readout so the canvas fills the screen
      if (w.tools.dropHint) w.tools.dropHint->setVisible(false);  // …and the hint (browser: .fullscreen-mode .drop-hint)
      // The image-size readout belongs to the tool rows; it returns with them (refreshStatusHintVisibility).
      if (w.tools.imageInfoHost) w.tools.imageInfoHost->setVisible(false);
      if (w.tools.imageInfoDock) w.tools.imageInfoDock->setVisible(false);   // …its pinned band with it
      w.selPanel->setVisible(false);   // hidden in fullscreen; revealed on right-edge hover
      w.fs.barsShown = false;
      w.fs.panelShown = false;
      w.fs.active = true;
      syncFullscreenGlyph();
      markFullscreenBars(true);
      beginFullscreenZoom();
      w.showFullScreen();
      w.setFocus(Qt::OtherFocusReason);   // help key events reach us for the Escape-exits path
      // The TOOL rows only — the header row stays away for the whole session, as the browser's fullscreen does.
      w.fs.bars.clear();
      for (QToolBar* b : w.findChildren<QToolBar*>())
        if (b != w.tools.headerToolbar) w.fs.bars.append(b);
      // One pass where the pointer already rests (the Fullscreen button sits in the top band).
      QTimer::singleShot(16, &w, [this] { fsHoverTick(); });
    }
    // Guarded so it never re-enters through a toggled slot.
    if (w.acts.fullscreen) { QSignalBlocker b(w.acts.fullscreen); w.acts.fullscreen->setChecked(w.fs.active); }
  }

  // Edge-hover reveal with hysteresis: a wide "keep" zone once shown stops flicker.
  void EditorView::fsHoverTick() {
    if (!w.fs.active) return;
    // A control dragged out of a revealed row keeps the rows up until its drop: a folded row hides
    // the button that holds the pointer (browser fullscreen/layer.js).
    if (support::anyIconDragActive()) {
      if (w.fs.tickAfterDrag) return;
      w.fs.tickAfterDrag = true;
      support::afterIconDrag([this, alive = QPointer<MainWindow>(&w)] {
        if (!alive) return;
        w.fs.tickAfterDrag = false;
        fsHoverTick();
      });
      return;
    }
    const QPoint p = w.mapFromGlobal(QCursor::pos());
    const QSize win = w.size();
    if (!FullscreenController::cursorInside(p, win)) return;
    // Drive off the tracked target (fs.barsShown), NOT isVisible(): a hiding bar stays visible until the slide ends, and
    // that would restart the hide every tick. The keep-zone is the revealed rows themselves plus a little grace.
    int tbBottom = 0;
    for (QToolBar* b : w.fs.bars)
      if (b->isVisible()) tbBottom = std::max(tbBottom, b->mapTo(&w, QPoint(0, b->height())).y());
    const int panelW = w.selPanel->isVisible() ? w.selPanel->width() : 0;
    const bool isOverPanel = w.fs.panelShown && p.x() > win.width() - panelW - DOCK_SEPARATOR_PX;
    const bool wantTb = w.fs.wantBars(p, tbBottom, isOverPanel);
    if (wantTb != w.fs.barsShown) {
      w.fs.barsShown = wantTb;
      animateBarsHeight(w.fs.bars, wantTb);   // reuse the pill's smooth height slide
    }

    // A 28px reveal band (5px was near-impossible to hit); the keep-zone is the panel itself, never narrower than a third of
    // the window, so dragging the splitter to resize it never auto-hides it mid-drag.
    const bool wantPnl = w.fs.wantPanel(p, win, panelW);
    if (wantPnl != w.fs.panelShown) {
      w.fs.panelShown = wantPnl;
      w.setPanelShown(wantPnl, true);
    }
  }

  // Pins the dock's own height too, or QMainWindow draws a drag grip above the canvas. Brackets OUT to enter, IN to leave (browser fullscreen/layer.js).
  void EditorView::syncFullscreenGlyph() {
    if (!w.acts.fullscreen) return;
    const QString name = w.fs.active ? QStringLiteral("minimize") : QStringLiteral("maximize");
    w.painted.iconNames.insert(w.acts.fullscreen, name);
    const QColor ink = w.toolButtonIconColor(w.acts.fullscreen, w.painted.iconColor);
    w.acts.fullscreen->setIcon(themedIcon(name, ink, TOOL_ICON));
  }

  // Qt matches property selectors at polish time, so the flag needs a re-polish.
  void EditorView::markFullscreenBars(bool on) {
    if (w.selPanel) w.selPanel->setCollapseChevronVisible(!on);
    for (QToolBar* b : w.findChildren<QToolBar*>()) {
      if (b == w.tools.headerToolbar) continue;
      b->setProperty("fsBar", on);
      b->style()->unpolish(b);
      b->style()->polish(b);
    }
    // …and the canvas frame with them: in fullscreen it wears the browser's fullscreen-panel
    // hairline (qss/app/shell.qss [fsView]).
    if (w.scroll) {
      w.scroll->setProperty("fsView", on);
      w.scroll->style()->unpolish(w.scroll);
      w.scroll->style()->polish(w.scroll);
    }
  }

  // The canvas starts at the size it APPEARED to be against the old viewport and rides to its true size; the zoom is preserved (browser parity).
  void EditorView::beginFullscreenZoom() {
    if (w.fs.zoomAnim) { w.fs.zoomAnim->stop(); w.fs.zoomAnim->deleteLater(); w.fs.zoomAnim = nullptr; }
    if (!w.canvas || !w.canvas->hasImage() || !w.scroll || !w.scroll->viewport()) {
      w.fs.zoomFromViewport = QSize();
      return;
    }
    w.fs.zoomFromViewport = w.scroll->viewport()->size();
    w.fs.zoomWaits = 0;
    QTimer::singleShot(0, &w, [this] { startFullscreenZoom(); });
  }

  void EditorView::startFullscreenZoom() {
    if (!w.fs.zoomFromViewport.isValid() || w.fs.zoomFromViewport.isEmpty()) return;
    if (!w.canvas || !w.canvas->hasImage() || !w.scroll || !w.scroll->viewport()) return;
    const QSize now = w.scroll->viewport()->size();
    if (now.isEmpty()) return;
    // showFullScreen()/showNormal() resize asynchronously on some platforms — wait for the new geometry, bounded.
    if (now == w.fs.zoomFromViewport) {
      if (++w.fs.zoomWaits > FullscreenController::ZOOM_WAIT_LIMIT) return;
      QTimer::singleShot(16, &w, [this] { startFullscreenZoom(); });
      return;
    }
    const auto ratio = FullscreenController::handoffRatio(w.fs.zoomFromViewport, now);
    w.fs.zoomFromViewport = QSize();   // consumed
    if (!ratio) return;
    const double target = w.canvas->getScale();
    const double start = core::clampScale(target * *ratio);
    if (std::abs(start - target) < 1e-4) return;

    auto* anim = new QVariantAnimation(&w);
    w.fs.zoomAnim = anim;
    anim->setDuration(support::flipMotion().ms);
    anim->setEasingCurve(support::flipMotion().easing);
    anim->setStartValue(start);
    anim->setEndValue(target);
    QObject::connect(anim, &QVariantAnimation::valueChanged, &w,
                     [this](const QVariant& v) { if (w.canvas) w.canvas->setScale(v.toDouble()); });
    QObject::connect(anim, &QVariantAnimation::finished, &w, [this, target] {
      w.fs.zoomAnim = nullptr;
      w.setZoom(target);   // land exactly on the user's zoom and resync the combo
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
  }

}  // namespace stencil::gui

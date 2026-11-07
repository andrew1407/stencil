#include "MainWindow.hpp"
#include "../../support/theme/faceSwap.hpp"
#include "../chrome/BoxGlide.hpp"
#include "EditorView.hpp"
#include "toolbarMetrics.hpp"
#include "iconSet.hpp"
#include "SelectionPanel.hpp"
#include "../../support/control/reveal/controlReveal.hpp"
#include "CanvasWidget.hpp"
#include "../../support/uiTimings.hpp"
#include "theme.hpp"
#include "imageAnchor.hpp"   // TOOLBAR_FOLDING_PROPERTY
#include "iconDrag.hpp"

#include <QApplication>
#include <QRegularExpression>
#include <QScrollArea>
#include <QLabel>
#include <QMenuBar>
#include <QPointer>
#include <QToolBar>

// Fullscreen, in its two kinds (the screen, or the window it is in): the toggle and the edge-hover reveal
// of the bars it hid, driven by the pointer's moves. The zoom hand-off is EditorViewFullscreenZoom.cpp.
namespace stencil::gui {

  void EditorView::toggleFullscreen() {
    if (!w.fs.active) w.fs.inWindow = false;
    flipFullscreen();
  }

  void EditorView::flipFullscreen() {
    // Whatever is in the air goes first: the switch hides every toolbar, and a cloud would be left over the bare canvas.
    stopDustClouds(&w);
    // In the window the view's box glides to its new room; the screen kind has the system's own flight.
    const bool glide = w.fs.inWindow && w.scroll && w.isVisible();
    const QRect glideFrom = glide ? QRect(w.scroll->mapTo(&w, QPoint(0, 0)), w.scroll->size()) : QRect();
    if (w.fs.active) {
      w.fs.active = false;
      syncFullscreenGlyph();
      markFullscreenBars(false);
      w.fs.bars.clear();
      // A leftover slide / fixed height would fight the restore below.
      if (barsAnim) { barsAnim->stop(); barsAnim->deleteLater(); barsAnim = nullptr; }
      w.parts.dockChrome.releaseBarsVeils();   // a pour cut short leaves its ground-coloured covers over the rows
      for (QToolBar* b : w.findChildren<QToolBar*>()) {
        b->setMinimumHeight(0);
        b->setMaximumHeight(QWIDGETSIZE_MAX);
        b->setProperty(TOOLBAR_FOLDING_PROPERTY, false);
      }
      w.fs.barsShown = false;
      w.fs.panelShown = false;
      fitCanvasToWindow(false);
      beginFullscreenZoom();
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
      // Last: macOS paints the window synchronously inside showNormal() for its exit snapshot, and keeps that frame (user report).
      const bool wasInWindow = w.fs.inWindow;
      w.fs.inWindow = false;
      if (!wasInWindow) w.showNormal();
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
      fitCanvasToWindow(true);
      beginFullscreenZoom();
      if (!w.fs.inWindow) w.showFullScreen();
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
    syncFullscreenTip();
    if (!glide) return;
    w.parts.dockChrome.settleFoldLayout();   // photographed as it now stands
    BoxGlide::play(&w, w.scroll, glideFrom, [this] { return w.fs.zoomAnim != nullptr; });
  }

  void EditorView::toggleFullscreenKind(bool inWindow) {
    if (!w.fs.active || w.fs.inWindow == inWindow) {
      if (!w.fs.active) w.fs.inWindow = inWindow;
      flipFullscreen();
      return;
    }
    // A switch: the rows stay hidden, only the window's own state moves, and the zoom rides across it.
    w.fs.inWindow = inWindow;
    beginFullscreenZoom();
    if (inWindow) w.showNormal(); else w.showFullScreen();
    syncFullscreenTip();
  }

  // Left in the mode's state by a system exit, a normal window kept its rows hidden. Only a real crossing counts:
  // our own switch already moved fs.active, so it reads as no change.
  void EditorView::followSystemFullscreen(Qt::WindowStates was, Qt::WindowStates now) {
    const bool on = (now & Qt::WindowFullScreen) != 0;
    if (((was & Qt::WindowFullScreen) != 0) == on || on == (w.fs.active && !w.fs.inWindow)) return;
    // The system took the screen from the in-window kind: adopt it rather than leave.
    if (on && w.fs.active) {
      w.fs.inWindow = false;
      syncFullscreenTip();
      return;
    }
    QTimer::singleShot(0, &w, [this] { toggleFullscreen(); });
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
    const int canvasLeft = w.scroll->mapTo(&w, QPoint(0, 0)).x(), canvasRight = canvasLeft + w.scroll->width();
    // A dialog up covers both bands, as the browser's modal overlay covers its triggers: nothing reveals.
    const bool modalUp = QApplication::activeModalWidget() != nullptr;
    const bool wantTb = !modalUp && w.fs.wantBars(p, tbBottom, isOverPanel, canvasLeft, canvasRight);
    if (wantTb != w.fs.barsShown) {
      w.fs.barsShown = wantTb;
      animateBarsHeight(w.fs.bars, wantTb);   // reuse the pill's smooth height slide
    }

    // A 28px reveal band (5px was near-impossible to hit); the keep-zone is the panel itself, never narrower than a third of
    // the window, so dragging the splitter to resize it never auto-hides it mid-drag.
    const int areaRight = panelW > 0 ? w.selPanel->mapTo(&w, QPoint(w.selPanel->width(), 0)).x() : canvasRight;
    const bool wantPnl = !modalUp && w.fs.wantPanel(p, win, panelW, areaRight);
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

  // Fullscreen's canvas fills the window edge to edge, as the browser's (fullscreen.css inset: 0): the window's
  // gutters showed its own ground round it, and a pointer flung into the right one missed the panel's band.
  void EditorView::fitCanvasToWindow(bool fill) {
    if (!w.centralLayout) return;
    w.centralLayout->setContentsMargins(0, 0, fill ? 0 : CENTRAL_SIDE_MARGIN, fill ? 0 : 14);
    if (QLayoutItem* row = w.centralLayout->count() ? w.centralLayout->itemAt(0) : nullptr)
      if (QLayout* l = row->layout()) l->setContentsMargins(fill ? 0 : 6, 0, 0, 0);
    if (w.selPanel) w.selPanel->setContentsMargins(0, 0, fill ? 0 : w.panelSlide.EDGE_GAP, 0);   // the list meets the edge
    if (!fill) w.parts.dockChrome.updatePanelReopenButton();   // the right inset follows the chevron again
  }

  // Qt matches property selectors at polish time, so the flag needs a re-polish.
  void EditorView::markFullscreenBars(bool on) {
    if (w.selPanel) w.selPanel->setCollapseChevronVisible(!on);
    // The host, and the panel's own named surfaces under it, which match through its flag.
    const auto mark = [on](QWidget* host, const char* name) {
      if (!host) return;
      host->setProperty(name, on);
      QList<QWidget*> restyle{host};
      for (QWidget* x : host->findChildren<QWidget*>(QRegularExpression(QStringLiteral("^selPanel")))) restyle << x;
      for (QWidget* x : restyle) gui::repolish(x);
    };
    for (QToolBar* b : w.findChildren<QToolBar*>()) if (b != w.tools.headerToolbar) mark(b, "fsBar");
    // The canvas loses its frame (qss/app/shell.qss [fsView]); the separator beside the panel takes the canvas's
    // ground [fsEditor]; the panel fills its height as the browser's #fs-points-panel does [fsPanel].
    mark(w.scroll, "fsView");
    if (w.scroll) gui::repolish(w.scroll, /*styleChange=*/true);   // QFrame re-reads its cached frame width
    mark(w.editor, "fsEditor");
    mark(w.selPanel, "fsPanel");
  }

}  // namespace stencil::gui

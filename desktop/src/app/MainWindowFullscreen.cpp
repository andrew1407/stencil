#include "MainWindow.hpp"
#include <QLabel>
#include <QMenuBar>
#include "MainWindow.hpp"
#include "ChatPlanTarget.hpp"
#include "LogoHoverFx.hpp"
#include "ChatMenuPanel.hpp"
#include "OpenImageDialog.hpp"
#include "CanvasWidget.hpp"
#include "ProjectsDialog.hpp"
#include "SelectionPanel.hpp"
#include "ShortcutsDialog.hpp"
#include "../support/DisintegrateOverlay.hpp"
#include "../support/controlReveal.hpp"
#include "../support/theme.hpp"

#include <QSignalBlocker>
#include <QToolBar>
#include <QToolButton>
#include <algorithm>

// Fullscreen: the toggle and the edge-hover reveal of the bars it hid.

namespace stencil::gui {

  void MainWindow::toggleFullscreen() {
    // Whatever is in the air goes first: the switch hides every toolbar, and a cloud would be left over the bare canvas.
    stopDustClouds(this);
    if (fs.active) {
      // Exit
      fs.active = false;
      syncFullscreenGlyph();
      markFullscreenBars(false);
      if (fs.hoverTimer) fs.hoverTimer->stop();
      // A leftover slide / fixed height would fight the restore below.
      if (barsAnim) { barsAnim->stop(); barsAnim->deleteLater(); barsAnim = nullptr; }
      for (QToolBar* b : findChildren<QToolBar*>()) { b->setMinimumHeight(0); b->setMaximumHeight(QWIDGETSIZE_MAX); }
      fs.barsShown = false;
      fs.panelShown = false;
      beginFullscreenZoom();
      showNormal();
      if (menuBar()) menuBar()->setVisible(true);
      if (status) status->setVisible(true);     // restore the coord readout
      if (dropHint) dropHint->setVisible(true);  // …and the drag & drop hint (browser: .drop-hint)
      // The size readout's DOCK too: its height is pinned, so hiding only the bar left an empty band.
      if (imageInfoHost) imageInfoHost->setVisible(true);
      if (imageInfoDock) imageInfoDock->setVisible(true);
      // The header row ALWAYS returns — it is the only way to re-show the tool rows.
      if (headerToolbar) { headerToolbar->setMaximumHeight(QWIDGETSIZE_MAX); headerToolbar->setVisible(true); }
      setToolbarsShown(fs.wasToolbars, false);
      // Sync the checks WITHOUT re-firing the animated toggled handlers.
      if (actToolbars) { QSignalBlocker b(actToolbars); actToolbars->setChecked(fs.wasToolbars); }
      if (actPanel) { QSignalBlocker b(actPanel); actPanel->setChecked(fs.wasPanel); }
      setPanelShown(fs.wasPanel, false);
      positionOverlayArrows();
    } else {
      // Enter: hide the top menu and the points panel; a cursor poll re-reveals each from its edge (browser parity).
      fs.wasToolbars = actToolbars ? actToolbars->isChecked() : true;
      fs.wasPanel = actPanel ? actPanel->isChecked() : true;   // was the panel expanded (vs rail)?
      // Capture the real width BEFORE hiding, or the edge reveal slides to setPanelShown's 320px default and snaps back.
      if (!panelAnim && selPanel->isVisible() && selPanel->width() > 120) panelRestoreWidth = selPanel->width();
      setToolbarsVisible(false);
      if (menuBar()) menuBar()->setVisible(false);
      if (status) status->setVisible(false);     // hide the coord readout so the canvas fills the screen
      if (dropHint) dropHint->setVisible(false);  // …and the hint (browser: .fullscreen-mode .drop-hint)
      // The image-size readout belongs to the tool rows; it returns with them (refreshStatusHintVisibility).
      if (imageInfoHost) imageInfoHost->setVisible(false);
      if (imageInfoDock) imageInfoDock->setVisible(false);   // …its pinned band with it
      selPanel->setVisible(false);   // hidden in fullscreen; revealed on right-edge hover
      fs.barsShown = false;
      fs.panelShown = false;
      fs.active = true;
      syncFullscreenGlyph();
      markFullscreenBars(true);
      beginFullscreenZoom();
      showFullScreen();
      setFocus(Qt::OtherFocusReason);   // help key events reach us for the Escape-exits path
      if (fs.hoverTimer) fs.hoverTimer->start(16);   // ~60Hz poll: reveal reacts immediately on hover
    }
    // Guarded so it never re-enters through a toggled slot.
    if (actFullscreen) { QSignalBlocker b(actFullscreen); actFullscreen->setChecked(fs.active); }
  }

  // Edge-hover reveal with hysteresis: a wide "keep" zone once shown stops flicker.
  void MainWindow::fsHoverTick() {
    if (!fs.active) return;
    const QPoint p = mapFromGlobal(QCursor::pos());
    const QSize win = size();
    if (!FullscreenController::cursorInside(p, win)) return;
    // Drive off the tracked target (fs.barsShown), NOT isVisible(): a hiding bar stays visible until the slide ends, and
    // that would restart the hide every tick. The keep-zone is the revealed rows themselves plus a little grace.
    int tbBottom = 0;
    for (QToolBar* b : findChildren<QToolBar*>())
      if (b != headerToolbar && b->isVisible())
        tbBottom = std::max(tbBottom, b->mapTo(this, QPoint(0, b->height())).y());
    const int panelW = selPanel->isVisible() ? selPanel->width() : 0;
    const bool isOverPanel = fs.panelShown && p.x() > win.width() - panelW - DOCK_SEPARATOR_PX;
    const bool wantTb = fs.wantBars(p, tbBottom, isOverPanel);
    if (wantTb != fs.barsShown) {
      fs.barsShown = wantTb;
      // The TOOL rows only — the header row stays away for the whole session, as the browser's fullscreen does.
      QList<QToolBar*> bars;
      for (QToolBar* b : findChildren<QToolBar*>())
        if (b != headerToolbar) bars.append(b);
      animateBarsHeight(bars, wantTb);   // reuse the pill's smooth height slide
    }

    // A 28px reveal band (5px was near-impossible to hit); the keep-zone is the panel itself, never narrower than a third of
    // the window, so dragging the splitter to resize it never auto-hides it mid-drag.
    const bool wantPnl = fs.wantPanel(p, win, panelW);
    if (wantPnl != fs.panelShown) {
      fs.panelShown = wantPnl;
      setPanelShown(wantPnl, true);
    }
  }

}  // namespace stencil::gui

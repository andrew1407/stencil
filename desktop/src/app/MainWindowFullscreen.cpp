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

#include <QSignalBlocker>
#include <QToolBar>
#include <QToolButton>
#include <algorithm>

// Fullscreen: the toggle and the edge-hover reveal of the bars it hid.

namespace stencil::gui {

  void MainWindow::toggleFullscreen() {
    // Whatever is in the air goes first: the switch hides every toolbar, and a cloud would be left over the bare canvas.
    stopDustClouds(this);
    if (fs_.active) {
      // Exit
      fs_.active = false;
      syncFullscreenGlyph();
      markFullscreenBars(false);
      if (fs_.hoverTimer) fs_.hoverTimer->stop();
      // A leftover slide / fixed height would fight the restore below.
      if (barsAnim_) { barsAnim_->stop(); barsAnim_->deleteLater(); barsAnim_ = nullptr; }
      for (QToolBar* b : findChildren<QToolBar*>()) { b->setMinimumHeight(0); b->setMaximumHeight(QWIDGETSIZE_MAX); }
      fs_.barsShown = false;
      fs_.panelShown = false;
      beginFullscreenZoom();
      showNormal();
      if (menuBar()) menuBar()->setVisible(true);
      if (status_) status_->setVisible(true);     // restore the coord readout
      if (dropHint_) dropHint_->setVisible(true);  // …and the drag & drop hint (browser: .drop-hint)
      // The size readout's DOCK too: its height is pinned, so hiding only the bar left an empty band.
      if (imageInfoHost_) imageInfoHost_->setVisible(true);
      if (imageInfoDock_) imageInfoDock_->setVisible(true);
      // The header row ALWAYS returns — it is the only way to re-show the tool rows.
      if (headerToolbar_) { headerToolbar_->setMaximumHeight(QWIDGETSIZE_MAX); headerToolbar_->setVisible(true); }
      setToolbarsShown(fs_.wasToolbars, false);
      // Sync the checks WITHOUT re-firing the animated toggled handlers.
      if (actToolbars_) { QSignalBlocker b(actToolbars_); actToolbars_->setChecked(fs_.wasToolbars); }
      if (actPanel_) { QSignalBlocker b(actPanel_); actPanel_->setChecked(fs_.wasPanel); }
      setPanelShown(fs_.wasPanel, false);
      positionOverlayArrows();
    } else {
      // Enter: hide the top menu and the points panel; a cursor poll re-reveals each from its edge (browser parity).
      fs_.wasToolbars = actToolbars_ ? actToolbars_->isChecked() : true;
      fs_.wasPanel = actPanel_ ? actPanel_->isChecked() : true;   // was the panel expanded (vs rail)?
      // Capture the real width BEFORE hiding, or the edge reveal slides to setPanelShown's 320px default and snaps back.
      if (selPanel_->isVisible() && selPanel_->width() > 120) panelRestoreWidth_ = selPanel_->width();
      setToolbarsVisible(false);
      if (menuBar()) menuBar()->setVisible(false);
      if (status_) status_->setVisible(false);     // hide the coord readout so the canvas fills the screen
      if (dropHint_) dropHint_->setVisible(false);  // …and the hint (browser: .fullscreen-mode .drop-hint)
      // The image-size readout belongs to the tool rows; it returns with them (refreshStatusHintVisibility).
      if (imageInfoHost_) imageInfoHost_->setVisible(false);
      if (imageInfoDock_) imageInfoDock_->setVisible(false);   // …its pinned band with it
      if (selPanel_->isVisible() && selPanel_->width() > 120) panelRestoreWidth_ = selPanel_->width();
      selPanel_->setVisible(false);   // hidden in fullscreen; revealed on right-edge hover
      fs_.barsShown = false;
      fs_.panelShown = false;
      fs_.active = true;
      syncFullscreenGlyph();
      markFullscreenBars(true);
      beginFullscreenZoom();
      showFullScreen();
      setFocus(Qt::OtherFocusReason);   // help key events reach us for the Escape-exits path
      if (fs_.hoverTimer) fs_.hoverTimer->start(16);   // ~60Hz poll: reveal reacts immediately on hover
    }
    // Guarded so it never re-enters through a toggled slot.
    if (actFullscreen_) { QSignalBlocker b(actFullscreen_); actFullscreen_->setChecked(fs_.active); }
  }

  // Edge-hover reveal with hysteresis: a wide "keep" zone once shown stops flicker.
  void MainWindow::fsHoverTick() {
    if (!fs_.active) return;
    const QPoint p = mapFromGlobal(QCursor::pos());
    const QSize win = size();
    if (!FullscreenController::cursorInside(p, win)) return;
    // Drive off the tracked target (fs_.barsShown), NOT isVisible(): a hiding bar stays visible until the slide ends, and
    // that would restart the hide every tick. The keep-zone is the revealed rows themselves plus a little grace.
    int tbBottom = 0;
    for (QToolBar* b : findChildren<QToolBar*>())
      if (b != headerToolbar_ && b->isVisible())
        tbBottom = std::max(tbBottom, b->mapTo(this, QPoint(0, b->height())).y());
    const bool wantTb = fs_.wantBars(p, tbBottom);
    if (wantTb != fs_.barsShown) {
      fs_.barsShown = wantTb;
      // The TOOL rows only — the header row stays away for the whole session, as the browser's fullscreen does.
      QList<QToolBar*> bars;
      for (QToolBar* b : findChildren<QToolBar*>())
        if (b != headerToolbar_) bars.append(b);
      animateBarsHeight(bars, wantTb);   // reuse the pill's smooth height slide
    }

    // A 28px reveal band (5px was near-impossible to hit); the keep-zone is the panel itself, never narrower than a third of
    // the window, so dragging the splitter to resize it never auto-hides it mid-drag.
    const int panelW = selPanel_->isVisible() ? selPanel_->width() : 0;
    const bool wantPnl = fs_.wantPanel(p, win, panelW);
    if (wantPnl != fs_.panelShown) {
      fs_.panelShown = wantPnl;
      setPanelShown(wantPnl, true);
    }
  }

}  // namespace stencil::gui
